#include <MsTimer2.h>

int waterServePins[8];//23,25,27...37
int readLickPins[8];//22,24,26...36
int waterServeMills[8] = {15, 19, 19, 20, 20, 20, 20, 20};  int* p_waterServeMills = waterServeMills;
int INTERRUPTPINS[] = {2, 3, 21, 20, 19, 18};
int INFARREDDETCETPIN = 49;
int PRESSLEVERPIN = 48;

int LICK_ACTIVE = HIGH;
int LICK_SILENCE = LOW;

#define LICK_REVERSE false
#if LICK_REVERSE
  LICK_ACTIVE = LOW;
  LICK_SILENCE = HIGH;
#endif

int waiting = 1;                      //int* p_waiting = &waiting;
int lick_mode = 0;                    int* p_lick_mode = &lick_mode;
int trial=0;                          int* p_trial = &trial;
int trial_set = 0;                    int* p_trial_set = &trial_set;//设为0时结束，设为1时开始, 设为2时按now_pos给水
int now_pos = -1;                     int* p_now_pos = &now_pos;//只管给水，不管屏幕显示
int lick_rec_pos = -1;                int* p_lick_rec_pos = &lick_rec_pos;
int water_flush = 0;                  int* p_water_flush = &water_flush;
int lick_count[8];                    int* p_lick_count = lick_count;
int waterserving = 0;

int tempLickPos = -1;
int tempTrialStautsMark = -1;
int lickEndThresholdMills = -1;

byte receivedData[256];
int index = 0;
bool isRecording = false;
//                        0           1            2             3            4             5           6           7                   8                      9      
int* pointer_array[]={p_lick_mode, p_trial, p_trial_set, p_now_pos, p_lick_rec_pos, p_water_flush};
int* pointerArrayType_array[]={p_waterServeMills, p_lick_count};
int  pointerArrayType_arrayLength[]={8};
String serial_print_type[]={"lick", "entrance", "press", "context_info", "log", "echo", "value_change", "command"};

int active_pin=-1;

template<class T>
int Length(T& arr){
  return sizeof(arr) / sizeof(arr[0]);
}

void SerialLog(String inputStr, String input_head=""){//"xxx"
  byte temp_buffer[256];
  int temp_length=stringToByteArray("log:"+input_head+inputStr, temp_buffer);
  Serial.write(temp_buffer, temp_length);
}

void serial_send(String inputStr){//"context_info:xxx" or ...
  byte temp_buffer[256];
  int temp_length=stringToByteArray(inputStr, temp_buffer);
  Serial.write(temp_buffer, temp_length);
  Serial.println();
}

void serial_send(byte inputArr[], int _length){
  Serial.write(inputArr, _length);
  Serial.println();
}

void pump_set(int pump_pin, int mills, bool write_mode=true){//write_mode: true:冲突则不设， false:冲突则覆盖
  if(pump_pin==active_pin && write_mode){
    //serial_send("log:pump set while pumping");
    return;
  }
  if(active_pin!=-1 && pump_pin!=active_pin){return;}//不允许当前timer在活动时设置其他针脚
  serial_send("log:pump set at "+String(pump_pin)+" lasting "+String(mills));
  digitalWrite(pump_pin, HIGH);
  active_pin=pump_pin;
  MsTimer2::set(mills, pump_set_call_by_interrupt);
  MsTimer2::start();
}

void pump_set_all(int mills, bool write_mode=false){//write_mode: true:冲突则不设， false:冲突则覆盖
  for(int i=0; i<Length(waterServePins); i++){
    digitalWrite(waterServePins[i], LOW);
  }
  active_pin = -2;
  MsTimer2::set(mills, pump_set_call_by_interrupt);
  MsTimer2::start();
}

void pump_set_call_by_interrupt(){
  if(active_pin == -2){
    for(int i=0; i<Length(waterServePins); i++){
    digitalWrite(waterServePins[i], LOW);
  }
  }else{
    digitalWrite(active_pin, LOW);
  }
  active_pin=-1;
  MsTimer2::stop();
}

void LickReportInInterrupt(){
  for (int i = 0; i < Length(readLickPins); i++) {
    if(digitalRead(readLickPins[i]) == LICK_ACTIVE){
      serial_send("lick:" + String(i)+":"+String(trial));
      lick_count[i]++;
      break;
    }
  }
}

void InfraRedInReportInInterrupt(){
  serial_send("entrance:"+String(trial)+":In");
}
void InfraRedLeaveReportInInterrupt(){
  serial_send("entrance:"+String(trial)+":leave");
}

void PressLeverReportInInterrupt(){
  serial_send("press:"+String(trial));
}

void print_status(String _head=""){
  // //                          0           1          2           3             4             5           6           7                   8                      9      
  // int* pointer_array[]={p_lick_mode, p_trial, p_trial_set, p_now_pos, p_lick_rec_pos, p_water_flush};
  char *log_buffer = new char[256];
  sprintf(log_buffer, " lick_mode %d, trial %d, trial_set %d, now_pos %d, water_serve_mode %d waiting %d",
                        lick_mode, trial, trial_set, now_pos, water_flush, waiting);
  String temp_log = "log:"+ _head+ log_buffer;
  temp_log += " lick_count: ";
  for(int i =0; i < 8; i++){
    temp_log += String(lick_count[i]);
    if(i<7){temp_log += ", ";}
  }
  serial_send(temp_log);
  delete log_buffer;
}

size_t stringToByteArray(String inputStr, byte* outputArray) {//return full length of byte_array
  String typeStr = inputStr.substring(0, inputStr.indexOf(':'));
  String contentStr = inputStr.substring(inputStr.indexOf(':') + 1);

  uint16_t typeId = -1;
  for(int i=0; i<Length(serial_print_type); i++){
    if(typeStr.equals(serial_print_type[i])){typeId=i;}
  }

  outputArray[0] = 0xAA;
  outputArray[1] = typeId & 0xFF;

  // 计算内容长度
  size_t contentLength = contentStr.length();

  //outputArray[4] = contentLength & 0xFF;
  outputArray[2] = contentLength & 0xFF;
//以下index均减2
  // 写入内容本身
  for (size_t i = 0; i < contentLength; ++i) {
    outputArray[3 + i] = (byte)contentStr[i];
  }

  // 添加结束标记
  outputArray[3 + contentLength] = 0xDD;

  return 4+contentLength;
}

String ByteArrayToCommand(byte byte_array[], int arraySize){//要求无前后补位
  //    0         1            2           3            4
  //{"move", "context_info", "log", "value_change", "command"};

  ////type: 2byte   length: 1byte    content: ...
  //type: 1yte   length: 1byte    content: ...
  // int temp_type = ((int)byte_array[1])*8 + (int)byte_array[0];
  // int temp_length = byte_array[2];
  int temp_type = (int)byte_array[0];
  int temp_length = byte_array[1];

  
  String result = "";
  //if(arraySize - 3 != temp_length){
  if(arraySize - 2 != temp_length){
    Serial.print("length mismatch: ");
    Serial.print(temp_type);
    Serial.print(" ");
    Serial.print(temp_length);
    Serial.print(" ");
    Serial.print(arraySize);
    Serial.print(" ");
    Serial.println();
    return result;}
  else{
    for(int i=2; i<temp_length+2; i++){
      result += (char)byte_array[i];
    }
    return result;
  }
}

int TrialStart(){
  waiting = 0;
  for(int i =0; i<Length(lick_count); i++){
    lick_count[i] = 0;
  }
  if(lick_mode==0){
    if(trial_set == 1){
      pump_set(waterServePins[now_pos], waterServeMills[now_pos]);
      trial_set = 0;
    }
  }
  //print_status("In trial start:");
  return 1;
}

int TrialEnd(){
  waiting = 1;
  now_pos = -1;
  //print_status("In trial end:");
  return 1;
}

void init_by_PC(bool init_all = false){
  waiting = 1;                      
  lick_mode = 0;                    
  trial=0;                          
  trial_set = 0;
  now_pos = -1;                     
  lick_rec_pos = -1;                
  water_flush = 0;                                     
  waterserving = 0;
  SerialLog("initialed");
}

void command_parse(String _command){
  // // Serial.print("in parse: ");
  // // Serial.print(_command);
  // Serial.println();
  //_command.replace("\r", "");
  _command.replace("\n", "");
  
  // Serial.println("in parse:");
  // Serial.println(_command);
  // Serial.println(_command.compareTo("check"));
  if(_command.compareTo("check")==0){print_status();}
  else if(_command.compareTo("init")==0) {init_by_PC();}
  else{
    int equal_pos=_command.indexOf('=');
    int arrayStartIndc=_command.indexOf('[');//format: 1[2]=0
    int arrayEndIndc = _command.indexOf(']');
    if(equal_pos>0){
      if(arrayStartIndc > 0){//array type variable change
        if(arrayEndIndc > 0){
          int input_ArrVar=(_command.substring(0, arrayStartIndc)).toInt();
          int input_ArrInd=(_command.substring(arrayStartIndc+1, arrayEndIndc)).toInt();
          int input_value=(_command.substring(equal_pos+1)).toInt();
          if(input_ArrInd >= pointerArrayType_arrayLength[input_ArrVar]){
            serial_send("log:invalid message!");
            return;
          }
          *(pointerArrayType_array[input_ArrVar]+sizeof(int)*input_ArrInd)=input_value;
          Serial.println("echo:"+_command+":echo");
          serial_send("echo:"+_command+":echo");
        }else{
          serial_send("log:invalid message!");
          return;
        }
      }else{// normal variable change
        //serial_send("log:in parse:"+_command);

        int input_var=(_command.substring(0, equal_pos)).toInt();
        int input_value=(_command.substring(equal_pos+1)).toInt();
        if(input_var>=0 && input_var<sizeof(pointer_array)){
          *(pointer_array[input_var])=input_value;
          Serial.println("echo:"+_command+":echo");
          serial_send("echo:"+_command+":echo");
          //pump_set(23, 100);
          if(pointer_array[input_var] == p_trial_set){
            tempTrialStautsMark = input_value;
            //serial_send("log:received, now tempTrialStautsMark = "+String(tempTrialStautsMark));
          }
        }
      }
    }
  }
}

void setup() {
  waiting = 1;                      
  lick_mode = 0;                    
  trial=0;                          
  trial_set = 0;
  now_pos = -1;                     
  lick_rec_pos = -1;                
  water_flush = 0;                                     
  waterserving = 0;

  for(int i = 0; i < 8; i ++){
    waterServePins[i] = 23 + i*2;
    readLickPins[i] = 22 + i*2;
    
    pinMode(waterServePins[i], OUTPUT);
    pinMode(readLickPins[i], INPUT);
    digitalWrite(readLickPins[i], LICK_SILENCE);
    digitalWrite(waterServePins[i], LOW);

  }

  // for(int i = 0; i < 5; i++){
  //   pinMode(INTERRUPTPINS[i], INPUT);
  //   attachInterrupt(i, LickReportInInterrupt, RISING);
  // }
    attachInterrupt(0, LickReportInInterrupt, RISING);
    attachInterrupt(1, InfraRedInReportInInterrupt, FALLING);
    attachInterrupt(3, InfraRedLeaveReportInInterrupt, RISING);
    attachInterrupt(2, PressLeverReportInInterrupt, FALLING);


  pinMode(50, OUTPUT);
  pinMode(INFARREDDETCETPIN, INPUT);
  pinMode(PRESSLEVERPIN, INPUT);
  digitalWrite(50, HIGH);
  digitalWrite(INFARREDDETCETPIN, HIGH);
  digitalWrite(PRESSLEVERPIN, HIGH);

  // initialize the LED pin as an output:
  Serial.begin(115200);
  Serial.println();
  Serial.println("initialed");
  Serial.println();
}

void loop() {
  if(tempTrialStautsMark != -1){//trial status update
    //serial_send("checking");
    if(tempTrialStautsMark == 1){//start
      TrialStart();
    }else { 
      if(tempTrialStautsMark == 2){//serve water and end
        pump_set(waterServePins[now_pos], waterServeMills[now_pos]);
        TrialEnd();
      }
      else if(tempTrialStautsMark == 0){//end
        TrialEnd();
      }
    }
    tempTrialStautsMark = -1;
  }

  if(!waiting){
    // for(int i = 0; i < 8; i++){
    //   if(digitalRead(readLickPins[i]) == LICK_ACTIVE){
    //       tempLickPos = i;
    //       lickEndThresholdMills = millis();
    //       break;
    //   }
    //   if(i == 7 ){//无舔水信号或舔结束
    //       if(tempLickPos != -1){
    //         lick_count[tempLickPos]++;
    //         serial_send("lick:" + String(tempLickPos)+":"+String(trial));
    //         tempLickPos = -1;
    //       }
    //   }
    // }

    if(lick_mode==0){
      //不用舔就出水
    }
    else if(lick_mode==1){
      //舔了才出水
    }
  }

  if(water_flush==2){

  }

  while (Serial.available()) { // 当有数据可读时
    byte inByte = (byte)Serial.read();
    // 检查是否接收到起始标志
    if (!isRecording && inByte == 0xAA) {
      isRecording = true;
    }else if (!isRecording) {
      Serial.println(inByte);
      if(inByte == 0x2F){
        byte secondInByte = (byte)Serial.read();
        if(secondInByte == 0x2F){
          Serial.println("received: //");
          //Serial.println(Serial.readString());
          command_parse(Serial.readString());
          break;
        }else {
          receivedData[index++] = secondInByte;
        }
      }
    }

    // 如果在记录模式下
    if (isRecording) {
      if (inByte == 0xDD) {
        command_parse(ByteArrayToCommand(receivedData, index));
        isRecording = false; // 结束记录
        index = 0; // 重置索引
        break;
      }else if (inByte == 0xAA) {
        isRecording = true;
        index = 0;
      }else{
        receivedData[index++] = inByte; // 将字符添加到数组
        //Serial.print(inByte);
      }
      
      // 防止数组越界
      if (index >= sizeof(receivedData)) {
        index = 0;
        isRecording = false;
      }
    }
  }
}