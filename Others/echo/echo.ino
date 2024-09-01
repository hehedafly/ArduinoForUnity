byte receivedData[256];
int index = 0;
bool isRecording = false;
String serial_print_type[]={"move", "context_info", "received", "log"};
String temp_string="log:789";

void command_parse(String _command){
  Serial.println(_command);
}

uint16_t stringToByteArray(String inputStr, byte* outputArray) {//return full length of byte_array
  String typeStr = inputStr.substring(0, inputStr.indexOf(':'));
  String contentStr = inputStr.substring(inputStr.indexOf(':') + 1);

  uint16_t typeId = -1;
  for(int i=0; i<sizeof(serial_print_type) / sizeof(serial_print_type[0]); i++){
    if(typeStr.equals(serial_print_type[i])){typeId=i;}
  }

  // 将类型ID转换为两个字节
  outputArray[0] = 0xAA;
  outputArray[1] = 0xBB;
  outputArray[2] = typeId & 0xFF;
  outputArray[3] = (typeId >> 8) & 0xFF;

  // 计算内容长度
  size_t contentLength = contentStr.length();

  outputArray[4] = contentLength & 0xFF;

  // 写入内容本身
  for (size_t i = 0; i < contentLength; ++i) {
    outputArray[5 + i] = (byte)contentStr[i];
  }

  // 添加结束标记
  outputArray[6 + contentLength] = 0xCC;
  outputArray[7 + contentLength] = 0xDD;

  return 8+contentLength;
}

String ByteArrayToCommand(byte byte_array[], int arraySize){//要求无前后补位
  //    0         1            2           3            4
  //{"move", "context_info", "log", "value_change", "command"};

  //type: 2byte   length: 1byte    content: ...
  int temp_type = ((int)byte_array[0])*8 + (int)byte_array[1];
  int temp_length = byte_array[2];
  String result = "";
  if(arraySize - 3 != temp_length){return "";}
  else{
    if(temp_type == 3){//更改变量值
      result += (char) byte_array[3];
      result += "="+ (char)byte_array[4];
    }else if(temp_type == 4){//字符串变量
      for(int i=3; i<temp_length; i++){
        result += (char)byte_array[i];
      }
    }
    return result;
  }
}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
}

void loop() {
  //byte temp_array[]= {0xAA, 0xBB, 0, 0, 3, 0, 32, 33, 34, 0xCC, 0xDD};
  // if(random(1000)<=2){
  //   byte temp_array[128];
  //   int temp_length=0;
  //   if(random(2)>1){temp_length=stringToByteArray(temp_string, temp_array);}
  //   else{temp_length=stringToByteArray(temp_string, temp_array);}

  //   for(int i=0; i<temp_length; i++){
  //     Serial.write(temp_array[i]);
  //   }
  //   Serial.write((byte)random(10));
  // }
  // put your main code here, to run repeatedly:
  while (Serial.available()) { // 当有数据可读时
    byte inByte = (byte)Serial.read();
    // 检查是否接收到起始标志
    if (!isRecording && inByte == 0xAA && Serial.peek() == 0xBB) {
      Serial.read(); // 跳过0xBB
      isRecording = true;
      index = 0;
    }

    // 如果在记录模式下
    if (isRecording) {
      receivedData[index++] = inByte; // 将字符添加到数组

      // 检查是否接收到结束标志并发送记录的数据
      if (inByte == 0xCC && Serial.peek() == 0xDD) {
        Serial.read(); // 跳过0xDD
        command_parse(ByteArrayToCommand(receivedData, sizeof(receivedData)/sizeof(receivedData[0])));
        //Serial.println(receivedData, index); // 通过串口发送记录的数据
        //Serial.println(); // 添加换行符以便于查看
        isRecording = false; // 结束记录
        index = 0; // 重置索引
      }
      
      // 防止数组越界
      if (index >= sizeof(receivedData)) {
        index = 0;
        isRecording = false;
      }
    }
  }
}


