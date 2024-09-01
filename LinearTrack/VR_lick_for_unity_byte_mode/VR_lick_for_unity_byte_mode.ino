#include <ADNS3080.h>
#include <MsTimer2.h>

#define read_lick_Pin 49
#define waterServePin 36    //原40 被用于2AFC ，响应仍是L pump
#define lickIndicatePin 30      //舔水指示
#define PIN_CS  23
#define PIN_RESET  25
#define ReadWaterServePin 29

int LICK_ACTIVE = 0;
int LICK_SILENCE = 1;

#define LICK_REVERSE false    //touch pannel导通为1，不导通为0，默认反相，导通为0不导通为1，设reverse为true为正相
#if LICK_REVERSE
  LICK_ACTIVE = 1;
  LICK_SILENCE = 0;
#endif

int count_water = 0;
int enter_reward_context = 0;         int* p_enter_reward_context = &enter_reward_context;
int in_reward_context = 0;            int* p_in_reward_context = &in_reward_context;
int lick_time_accu = 0;               int* p_lick_time_accu = &lick_time_accu;
int water_flush = 0;                  int* p_start_water = &water_flush;
int lick_count = 0;                   int* p_lick_count = &lick_count;
int lick_count_max=0;                 int* p_lick_count_max=&lick_count_max;
//unsigned long timelimit = 1000;
unsigned long previousMillis = 0;
long interval = 0;//舔和给水之间的间隔
unsigned long previousMillis3 = 0;
const long interval_samplerate = 10;//samplerate 10ms
unsigned long previousMillis4 = 0;
const int fixed_times = 50;
bool waterserving = false;
//int tr1 = 0;
int lick_mode=0;                  int* p_lick_mode = &lick_mode; //0：在奖励区域条件給水，1：随时根据条件給水
int lick_mode0_delay=0;           int* p_lick_mode0_delay = &lick_mode0_delay;
int lick_mode1_delay=0;           int* p_lick_mode1_delay = &lick_mode1_delay;//设定给水最低间隔,设为负数则停止舔舐给水，-2时给水并恢复至-1
int trial=0;                      int* p_trial = &trial;

// String inputString = "";      // a String to hold incoming data
// bool stringComplete = false;
byte receivedData[256];
int index = 0;
bool isRecording = false;
//                          0                         1                   2               3             4             5           6           7                   8                      9      
int* pointer_array[]={p_enter_reward_context, p_in_reward_context, p_lick_time_accu, p_lick_count, p_start_water, p_lick_mode, p_trial, p_lick_count_max, p_lick_mode0_delay, p_lick_mode1_delay};
String serial_print_type[]={"move", "context_info", "log", "echo", "value_change", "command"};

int active_pin=-1;

ADNS3080 <PIN_RESET, PIN_CS> sensor;
float sensorRefreshTime = 1000*60*10;//10min reset一次
float sensorResetMillsNow = 0;

void SerialLog(String inputStr, String input_head=""){//"xxx"
  byte temp_buffer[256];
  int temp_length=stringToByteArray("log:"+input_head+inputStr, temp_buffer);
  Serial.write(temp_buffer, temp_length);
}

void serial_send(String inputStr){//"context_info:xxx"
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
  if(pump_pin==active_pin && write_mode){return;}
  if(active_pin!=-1 && pump_pin!=active_pin){return;}//不允许当前timer在活动时设置其他针脚
  digitalWrite(waterServePin, LOW);
  active_pin=pump_pin;
  MsTimer2::set(mills, pump_set_call_by_interrupt);
  MsTimer2::start();
}

void pump_set_call_by_interrupt(){
  digitalWrite(active_pin, HIGH);
  active_pin=-1;
  MsTimer2::stop();
}

void init_by_PC(bool init_all = false){
  count_water = 0;
  enter_reward_context = 0;  
  in_reward_context = 0; 
  lick_time_accu = 0;    
  water_flush = 0;       
  lick_count = 0;        
  lick_count_max=0;
  sensor.reset();
  sensor.setup();
  SerialLog("initialed");
  if(init_all){
    previousMillis = 0;
    previousMillis3 = 0;
    previousMillis4 = 0;
    //int tr1 = 0;
    lick_mode=0;           
    lick_mode0_delay=0;    
    lick_mode1_delay=0;    
    trial=0;               
    digitalWrite(waterServePin, HIGH);
    digitalWrite(lickIndicatePin, LOW);
  }
}

void print_status(String _head=""){
  //                          0                         1                   2               3             4             5           6           7                   8                      9      
  //int* pointer_array[]={p_enter_reward_context, p_in_reward_context, p_lick_time_accu, p_lick_count, p_start_water, p_lick_mode, p_trial, p_lick_count_max, p_lick_mode0_delay, p_lick_mode1_delay};
  char *log_buffer = new char[256];
  sprintf(log_buffer, " enter_zone %d, in_zone %d, lick_time_accu %d, lick_count %d, satrt_water %d, lick_mode %d, trial  %d, lick_count_max %d, p_lick_mode0_delay %d, p_lick_mode1_delay %d",
                        enter_reward_context, in_reward_context, lick_time_accu, lick_count, water_flush, lick_mode, trial, lick_count_max, lick_mode0_delay, lick_mode1_delay);
  String temp_log = "log:"+ _head+ log_buffer;
  serial_send(temp_log);
  delete log_buffer;
}

void command_parse(String _command){
  // serial_send("log:in parse:"+_command);
  // // Serial.print("in parse: ");
  // // Serial.print(_command);
  // Serial.println();
  //_command.replace("\r", "");
  if(_command.compareTo("check")==0){print_status();}
  else if(_command.compareTo("init")==0) {init_by_PC();}
  else{
    //print_status("pre:   ");
    int equal_pos=_command.indexOf('=');

    if(equal_pos>0){
      int input_var=(_command.substring(0, equal_pos)).toInt();
      int input_value=(_command.substring(equal_pos+1)).toInt();
      if(input_var>=0 && input_var<sizeof(pointer_array)){
        *(pointer_array[input_var])=input_value;
        Serial.println("echo:"+_command+":echo");
        serial_send("echo:"+_command+":echo");

        // if(pointer_array[input_var] == p_lick_mode){

        // }
      }
    }
    //print_status("after: ");
  }
}

size_t stringToByteArray(String inputStr, byte* outputArray) {//return full length of byte_array
  String typeStr = inputStr.substring(0, inputStr.indexOf(':'));
  String contentStr = inputStr.substring(inputStr.indexOf(':') + 1);

  uint16_t typeId = -1;
  for(int i=0; i<sizeof(serial_print_type) / sizeof(serial_print_type[0]); i++){
    if(typeStr.equals(serial_print_type[i])){typeId=i;}
  }

  outputArray[0] = 0xAA;
  // outputArray[1] = 0xBB;
  // outputArray[2] = typeId & 0xFF;//低位
  // outputArray[3] = (typeId >> 8) & 0xFF;//高位
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
  //outputArray[4 + contentLength] = 0xCC;
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

void setup() {
  // initialize the LED pin as an output:
  pinMode(read_lick_Pin, INPUT);
  pinMode(waterServePin, OUTPUT);
  pinMode(lickIndicatePin, OUTPUT);
  digitalWrite(waterServePin, HIGH);
  digitalWrite(lickIndicatePin, LOW);
  //digitalWrite(read_lick_Pin, LICK_SILENCE);
  sensor.reset();
  sensor.setup();
  // initialize serial communications:
  Serial.begin(115200);
  delay(1);
  Serial.println();
  Serial.println("initialed");
  Serial.println();
}

void loop() {
  int8_t dx, dy;      // Displacement since last function call
  dx=0; dy=0;
  sensor.displacement( &dx, &dy );
  dx+=64;dy+=64;
  if(dx!=64 || dy!=64){
    byte move_buffer[32];
    int temp_length = stringToByteArray("move:xy", move_buffer);
    move_buffer[temp_length-3]=(byte)dx;
    move_buffer[temp_length-2]=(byte)dy;
    serial_send(move_buffer, temp_length);
  }
  if(dx ==0 && dy == 0 && millis() - sensorResetMillsNow >= sensorRefreshTime){
    sensor.reset();
    sensor.setup();
    sensorResetMillsNow = millis();
  }

  if (enter_reward_context>0) {
    lick_count=in_reward_context==0? lick_count_max: lick_count;
    //serial_send("lick_count updated");
    //trial=(trial+(in_reward_context==0? 1: 0));
    in_reward_context = 1;
    previousMillis = millis();
    enter_reward_context=0;
  }
  else if (enter_reward_context == -1) {
    enter_reward_context=0;
    in_reward_context=0;
    lick_count=0;
  }

  if(digitalRead(ReadWaterServePin) == HIGH){
    //serial_send("context_info:ws");
    if(waterserving == false){
      serial_send("context_info:ws");
    }
    waterserving = true;
  }else{
    waterserving = false;
  }
    
  unsigned long currentMillis3 = millis();
  if (currentMillis3 - previousMillis3 >= interval_samplerate) {//interval起每次监测舔的间隔作用//10ms采样一次
    //Serial.println(digitalRead(read_lick_Pin));
    if(digitalRead(read_lick_Pin) == LICK_ACTIVE){lick_time_accu++;}
    //lick_time_accu += digitalRead(read_lick_Pin) == 0? 1: 0;
    digitalWrite(lickIndicatePin, digitalRead(read_lick_Pin) == LICK_ACTIVE? HIGH: LOW);
    //tr1 += 1;
    //if (tr1>=9182){tr1=fixed_times+1;}
    if (lick_mode1_delay>0){lick_mode1_delay--;}
    if (lick_mode0_delay>0){lick_mode0_delay--;}
    previousMillis3 = currentMillis3;
    //digitalWrite(waterServePin, HIGH);
    //start_water=0;//仅调试，后续删除
  }

  /////////////////////////////////////////////////////////////////////////////////判断舔水
  if(lick_mode==0){
    if (in_reward_context){
      unsigned long currentMillis = millis();
      if (currentMillis - previousMillis >= interval){  //interval=0表示不需要delay，到位置舔了直接给
        if (digitalRead(read_lick_Pin) == LICK_SILENCE && lick_time_accu>0) {//完成一次舔
          //tr1 = 0;                                      //初始化判断标准
          // if ((lick_time_accu >= 1) && (lick_time_accu <= 50)){  //test lick, 50是多次舔的总时间，如果过长也没奖励
          if(lick_mode0_delay==0){
            lick_count -= 1; 
            if(lick_count>=0){
              pump_set(waterServePin, 5);
              serial_send("context_info:lick:correct");
              lick_mode0_delay=50;
            }
          }
          lick_time_accu = 0;
          serial_send("context_info:lick:");
          // }

          // lick_time_accu = 0;
        }
      }
    }else if(!in_reward_context){
      unsigned long currentMillis = millis();
      if (currentMillis - previousMillis >= interval){  //interval=0表示不需要delay，到位置舔了直接给
        if (digitalRead(read_lick_Pin)==LICK_SILENCE && lick_time_accu>0) {//完成一次舔
          //if ((lick_time_accu >= 1) && (lick_time_accu <= 50)){
            serial_send("context_info:lick:");
            lick_time_accu = 0;
          //}
        }
      }
    }
  }
  ////////////////////////////////////////////////////////lick mode 0按奖励位置舔则给水
  else if(lick_mode==1){
    if (digitalRead(read_lick_Pin) == LICK_SILENCE && lick_time_accu>0){//按舔给水
      lick_time_accu=0;
      serial_send("context_info:lick:");
      if(lick_mode1_delay==0){
        lick_mode1_delay=50;//设定给水最低有50*10ms间隔
        //pump_set(waterServePin, 5);
        serial_send("context_info:lick:correct");
      }
    }
    if(lick_mode1_delay>0){lick_mode1_delay--;}

    if(lick_mode1_delay==-2){//根据条件强制给水，与delay无关
      pump_set(waterServePin, 5);
      lick_mode1_delay=-1;
    }
  }
  ////////////////////////////////////////////////////////////lick mode 1 速度到了给水

  if(water_flush==2){
    pump_set(waterServePin, 5);
  }

  while (Serial.available()) { // 当有数据可读时
    byte inByte = (byte)Serial.read();
    // 检查是否接收到起始标志
    //if (!isRecording && inByte == 0xAA && Serial.peek() == 0xBB) {
    if (!isRecording && inByte == 0xAA) {
      //Serial.read(); // 在判断时再跳过0xBB
      isRecording = true;

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

// void serialEvent() {
//   while (Serial.available()) {
//     char inChar = (char)Serial.read();
//     if (inChar == '\n') {
//       stringComplete = true;
//     }
//     else{inputString += inChar;}
//   }
// }