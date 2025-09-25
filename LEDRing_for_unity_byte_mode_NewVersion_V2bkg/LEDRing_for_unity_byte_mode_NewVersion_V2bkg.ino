#include <DueTimer.h>
#include <RingBuf.h>

int waterServePins[8];//22,24,26...36
int readLickPins[8];//23,25,27...37
int waterServeMicros[8] = {20000, 20000, 20000, 20000, 20000, 20000, 20000, 20000};      int* p_waterServeMicros = waterServeMicros;

const int INFARREDDETCETPIN = 49;
const int PRESSLEVERPIN = 48;
const int OGOUTPIN = 40;
const int MSRECORDPIN = 41;
const int SYNCINPUTPIN = 42;
String VERSION = "V2";
// const int SINGNALOUTPINS[8];
// const int SINGNALINPUTPINS[8];

int LICK_ACTIVE = HIGH;
int LICK_SILENCE = LOW;

int waiting = 1;                      //int* p_waiting = &waiting;
int lick_mode = 0;                    int* p_lick_mode = &lick_mode;
int trial=0;                          int* p_trial = &trial;
int trial_set = 0;                    int* p_trial_set = &trial_set;//设为0时结束，设为1时开始, 设为2时按now_pos给水
int now_pos = -1;                     int* p_now_pos = &now_pos;//只管给水，不管屏幕显示
int lick_rec_pos = -1;                int* p_lick_rec_pos = &lick_rec_pos;
int water_flush = 0;                  int* p_water_flush = &water_flush;
int lick_count[8];                    int* p_lick_count = lick_count;
int OGActiveMills = 100;              int* p_OGActiveMills = &OGActiveMills;
int waterserving = 0;
int miniscopeRecord = 0;              int* p_miniscopeRecord = &miniscopeRecord;

int INDEBUGMODE = 0;                  int* p_INDEBUGMODE = &INDEBUGMODE;

int tempLickPos = -1;
int tempTrialStautsMark = -1;
int lickEndThresholdMills = -1;

byte receivedData[512];
RingBuf<char, 1024> SendDataBuffer;
// volatile RingBuf<String, 128> SyncDataBuffer;
volatile bool SyncMark = false;
int indexInSerial = 0;
bool isRecording = false;
bool plainTextMark = false;
//                        0           1            2             3            4             5           6           7                   8                      9      
int* pointer_array[]={p_lick_mode, p_trial, p_trial_set, p_now_pos, p_lick_rec_pos, p_water_flush, p_INDEBUGMODE, p_OGActiveMills, p_miniscopeRecord};
int* pointerArrayType_array[]={p_waterServeMicros, p_lick_count};
int  pointerArrayType_arrayLength[]={8, 8};

//                                  0         1          2           3            4      5          6             7          8          9       10              11  
const char* serial_print_type[]={"lick", "entrance", "press", "context_info", "log", "echo", "value_change", "command", "debugLog", "stay", "syncInfo", "miniscopeStart"};

DueTimer pumpTimer = Timer.getAvailable();
DueTimer OGTimer = Timer.getAvailable();
volatile int active_pin=-1;

template<class T>
int Length(T& arr){
  return sizeof(arr) / sizeof(arr[0]);
}

void SerialLog(String inputStr, String input_head=""){//"xxx"
  byte temp_buffer[256];
  int temp_length=stringToByteArray("log:"+input_head+inputStr, temp_buffer);
  Serial.write(temp_buffer, temp_length);
}

bool serial_sendRaw(byte inputArr[], int _length){
  size_t wl = Serial.write(inputArr, _length);
  Serial.println();
  // Serial.print("wl:");
  // Serial.print(wl);
  // Serial.println();

  return wl == _length;
  // Serial.println(_length);
}

bool serial_send(String inputStr){//"context_info:xxx" or ...
  byte temp_buffer[256];
  int temp_length=stringToByteArray(inputStr, temp_buffer);
  return serial_sendRaw(temp_buffer, temp_length);
  // delete temp_buffer;
  // delayMicroseconds(10);
}

void pump_set(int pump_pin, int micros, bool write_mode=true){//write_mode: true:冲突则不设， false:冲突则覆盖
  if(pump_pin==active_pin && write_mode){
    //serial_send("log:pump set while pumping");
    return;
  }
  if(active_pin!=-1 && pump_pin!=active_pin){return;}//不允许当前timer在活动时设置其他针脚
  if(INDEBUGMODE > 0){
    serial_send("debugLog:pump set at "+String(pump_pin)+" lasting "+String(micros));
  }
  
  digitalWrite(pump_pin, HIGH);
  digitalWrite(13, HIGH);
  active_pin=pump_pin;
  pumpTimer.attachInterrupt(pump_set_call_by_interrupt).start(micros);
}

void pump_set_all(int micros, bool write_mode=false){//write_mode: true:冲突则不设， false:冲突则覆盖
  for(int i=0; i<Length(waterServePins); i++){
    digitalWrite(waterServePins[i], HIGH);
  }
  active_pin = -2;
  pumpTimer.attachInterrupt(pump_set_call_by_interrupt).start(micros);

}

void pump_set_call_by_interrupt(){
  if(INDEBUGMODE > 0){
    bufferPushChar("debugLog:pump finish in timer");
  }
  if(active_pin == -2){
    for(int i=0; i<Length(waterServePins); i++){
    digitalWrite(waterServePins[i], LOW);
  }
  }else{
    digitalWrite(active_pin, LOW);
  }
  active_pin=-1;
  digitalWrite(13, LOW);
  pumpTimer.stop();
}

void OG_set(){//-1:持续, 0:关闭, 1+:持续mills
  if(INDEBUGMODE > 0){
    char _content[sizeof("debugLog:OG set at lasting ") + 8] = "";
    sprintf(_content, "debugLog:OG set at %d lasting %d", OGOUTPIN, OGActiveMills);
    bufferPushChar(_content);
    // bufferPushChar("debugLog:OG set at "+String(OGOUTPIN)+" lasting "+String(OGActiveMills));
  }
  
  if(OGActiveMills > 0){
    digitalWrite(OGOUTPIN, HIGH);
    OGTimer.attachInterrupt(OG_set_call_by_interrupt).start(OGActiveMills*1000);
  }else {
    if(OGActiveMills == 0){
      digitalWrite(OGOUTPIN, LOW);
      OGTimer.attachInterrupt(OG_set_call_by_interrupt).stop();
    }else {
      digitalWrite(OGOUTPIN, HIGH);
    }
  }
}

void OG_set_call_by_interrupt(){
  if(INDEBUGMODE > 0){
    bufferPushChar("debugLog:OG finish in timer");
  }
  digitalWrite(OGOUTPIN, LOW);
  OGTimer.attachInterrupt(OG_set_call_by_interrupt).stop();
}

void Sync_call_by_interrupt(){
  SyncMark = true;
}

// void LickReportInInterrupt(){
//   for (int i = 0; i < Length(readLickPins); i++) {
//     // Serial.print(i);
//     // Serial.println(digitalRead(readLickPins[i]));
//     if(digitalRead(readLickPins[i]) == LICK_ACTIVE){
//       serial_send("lick:" + String(i)+":"+String(trial));
//       lick_count[i]++;
//       return;
// }
//   }
//   Serial.println("no signal");
// }

// void LickReportInInterrupt0(){serial_send("lick:" + String(0)+":"+String(trial));lick_count[0]++;return;}
// void LickReportInInterrupt1(){serial_send("lick:" + String(1)+":"+String(trial));lick_count[1]++;return;}
// void LickReportInInterrupt2(){serial_send("lick:" + String(2)+":"+String(trial));lick_count[2]++;return;}
// void LickReportInInterrupt3(){serial_send("lick:" + String(3)+":"+String(trial));lick_count[3]++;return;}
// void LickReportInInterrupt4(){serial_send("lick:" + String(4)+":"+String(trial));lick_count[4]++;return;}
// void LickReportInInterrupt5(){serial_send("lick:" + String(5)+":"+String(trial));lick_count[5]++;return;}
// void LickReportInInterrupt6(){serial_send("lick:" + String(6)+":"+String(trial));lick_count[6]++;return;}
// void LickReportInInterrupt7(){serial_send("lick:" + String(7)+":"+String(trial));lick_count[7]++;return;}
void LickReportInInterrupt0(){char entry[16];sprintf(entry, "lick:%d:%d", 0, trial);bufferPushChar(entry);lick_count[0]++;return;}
void LickReportInInterrupt1(){char entry[16];sprintf(entry, "lick:%d:%d", 1, trial);bufferPushChar(entry);lick_count[1]++;return;}
void LickReportInInterrupt2(){char entry[16];sprintf(entry, "lick:%d:%d", 2, trial);bufferPushChar(entry);lick_count[2]++;return;}
void LickReportInInterrupt3(){char entry[16];sprintf(entry, "lick:%d:%d", 3, trial);bufferPushChar(entry);lick_count[3]++;return;}
void LickReportInInterrupt4(){char entry[16];sprintf(entry, "lick:%d:%d", 4, trial);bufferPushChar(entry);lick_count[4]++;return;}
void LickReportInInterrupt5(){char entry[16];sprintf(entry, "lick:%d:%d", 5, trial);bufferPushChar(entry);lick_count[5]++;return;}
void LickReportInInterrupt6(){char entry[16];sprintf(entry, "lick:%d:%d", 6, trial);bufferPushChar(entry);lick_count[6]++;return;}
void LickReportInInterrupt7(){char entry[16];sprintf(entry, "lick:%d:%d", 7, trial);bufferPushChar(entry);lick_count[7]++;return;}

void InfraRedInReportInInterrupt(){
  char _content[sizeof("entrance::In") + 8] = "";
  sprintf(_content, "entrance:%d:In", trial);
  bufferPushChar(_content);
}
void InfraRedLeaveReportInInterrupt(){
  char _content[sizeof("entrance::leave") + 8] = "";
  sprintf(_content, "entrance:%d:leave", trial);
  bufferPushChar(_content);
  // bufferPushChar("entrance:"+String(trial)+":leave");
}

void PressLeverReportInInterrupt(){
  char _content[sizeof("press:") + 8] = "";
  sprintf(_content, "press:%d", trial);
  bufferPushChar(_content);
  // bufferPushChar("press:"+String(trial));
}

bool bufferPushString(String strmsg){
  bufferPushChar(strmsg.c_str());
}

bool bufferPushChar(const char* msg){
  // Serial.print("in write:");
  // Serial.print(msg);
  // return false;
  size_t len = strlen(msg);  // 包含终止符 '\0'
  len = min(len, 0xff);
  // Serial.println(len);
  // Serial.print(strlen(msg));
  // Serial.println(msg);
  SendDataBuffer.push((char)(len + 1));      // 写入长度前缀
  for(int i = 0; i < len; i++){
    if(! SendDataBuffer.push(msg[i])){
      return false;
    }    // 写入实际数据
  }
  SendDataBuffer.push((char)0);
  // Serial.print("now size:");
  // Serial.println(SendDataBuffer.size());
  return true;
}

String bufferGetString(){
  char _len;
  char _end = 'x';
  // Serial.print("in read: ");
  // Serial.println(SendDataBuffer.size());
  // SendDataBuffer.clear();
  // return "test";

  SendDataBuffer.lockedPop(_len);  // 读取长度前缀
  int len = (int)_len;
  // Serial.println(len);
  if (SendDataBuffer.size() < len){
    Serial.print("error in read with length");
    Serial.println(len);

    return "";  // 数据不完整
  }
  else if(SendDataBuffer.lockedPeek(_end, _len - 1)){
    if(_end != 0){
      Serial.print("wrong end: ");
      Serial.println(_end);
      return "";
    }
  }
  
  char data[len-1];
  for(int i = 0; i < len; i++){
    char _t;
    SendDataBuffer.lockedPop(_t);
    data[i] = _t;
  }
  // Serial.println(SendDataBuffer.size());

  return data;
}

void print_status(String _head=""){
  // //                          0           1          2           3             4             5           6           7                   8                      9      
  // int* pointer_array[]={p_lick_mode, p_trial, p_trial_set, p_now_pos, p_lick_rec_pos, p_water_flush, p_INDEBUGMODE};
  char *log_buffer = new char[256];
  sprintf(log_buffer, " lick_mode %d, trial %d, trial_set %d, now_pos %d, water_serve_mode %d INDEBUGMODE %d waiting %d" ,
                        lick_mode, trial, trial_set, now_pos, water_flush, INDEBUGMODE, waiting);
  String temp_log = "debugLog:"+ _head+ log_buffer;
  
  serial_send(temp_log);
  delete log_buffer;
}

void print_ArrayStatus(String _head=""){
  // //                          0           1          2           3             4             5           6           7                   8                      9      
  // int* pointer_array[]={p_lick_mode, p_trial, p_trial_set, p_now_pos, p_lick_rec_pos, p_water_flush};
  String temp_log = "waterServeMicros: ";
  for(int i =0; i < Length(waterServeMicros); i++){
    temp_log += String(waterServeMicros[i]);
    if(i<7){temp_log += ", ";}
  }

  temp_log += "; lick_count: ";
  for(int i =0; i < 8; i++){
    temp_log += String(lick_count[i]);
    if(i<7){temp_log += ", ";}
  }
  
  temp_log = "debugLog:"+ _head+ temp_log;
  serial_send(temp_log);
}

size_t stringToByteArray(String inputStr, byte* outputArray) {//return full length of byte_array
    // Serial.print("in to array: ");

    // Serial.println(inputStr);

  String typeStr = inputStr.substring(0, inputStr.indexOf(':'));
  String contentStr = inputStr.substring(inputStr.indexOf(':') + 1);
  uint16_t typeId = -1;
  // for(int i=0; i<Length(serial_print_type); i++){
  //   if(typeStr.equals(serial_print_type[i])){typeId=i;}
  // }
  for (int i = 0; i < (sizeof(serial_print_type) / sizeof(serial_print_type[0])); i++) {
    // 2. 使用 strcmp 比较 C 风格字符串
    if (strcmp(typeStr.c_str(), serial_print_type[i]) == 0) {
        typeId = i;
        break; // 找到后立即退出循环
    }
  }

  outputArray[0] = 0xAA;
  outputArray[1] = typeId & 0xFF;

  // 计算内容长度
  size_t contentLength = min(0xff, contentStr.length());
  // Serial.print(contentStr);
  // Serial.println(contentLength);
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

String ByteArrayToCommand(byte byte_array[], int arraySize){//要求无前后补位，不转化type为对应文字
  //type: 1yte   length: 1byte    content: ...
  // int temp_type = ((int)byte_array[1])*8 + (int)byte_array[0];
  // int temp_length = byte_array[2];
  int temp_type = (int)byte_array[0];
  int temp_length = byte_array[1];

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
    return "";}
  else{
    char res[temp_length];
    memcpy(res, byte_array+2, temp_length);
    return String(res);
  }
}

int TrialStart(){
  // serial_send("debugLog:trial start");
  waiting = 0;
  for(int i =0; i<Length(lick_count); i++){
    lick_count[i] = 0;
  }
  if(lick_mode==0){
    if(trial_set == 1 && now_pos != -1){
      pump_set(waterServePins[now_pos], waterServeMicros[now_pos]);
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

void commandParse(String _command){
  // // Serial.print("in parse: ");
  // // Serial.print(_command);
  // Serial.println();
  //_command.replace("\r", "");
  _command.replace("\n", "");
  
  // Serial.println("in parse:");
  // Serial.println(_command);
  // Serial.println(_command.compareTo("check"));
  int equal_pos=_command.indexOf('=');
  if(_command.compareTo("check")==0){print_status();}
  else if(_command.compareTo("checkArray")==0){print_ArrayStatus();}
  else if(_command.compareTo("init")==0) {init_by_PC();}
  else if(equal_pos>0 && _command.substring(0, equal_pos).compareTo("sw")==0){
    int input_value=(_command.substring(equal_pos+1)).toInt();
    pump_set(waterServePins[input_value], waterServeMicros[input_value]);
  }
  else{
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
          *(pointerArrayType_array[input_ArrVar] + input_ArrInd)=input_value;
          char echoContent[256] = "";
          sprintf(echoContent, "echo:%s:echo", _command.c_str());
          Serial.println(echoContent);
          // Serial.println(input_value);
          // serial_send("echo:"+_command+":echo");
          if(INDEBUGMODE > 0){
            // Serial.flush();
            print_ArrayStatus();
            // Serial.flush();
          }
        }else{
          serial_send("log:invalid message!");
          return;
        }
      }else{// normal variable change
        //serial_send("debugLog:in parse:"+_command);

        int input_var=(_command.substring(0, equal_pos)).toInt();
        int input_value=(_command.substring(equal_pos+1)).toInt();
        if(input_var>=0 && input_var<sizeof(pointer_array)){
          *(pointer_array[input_var])=input_value;
          Serial.println("echo:"+_command+":echo");

          if(INDEBUGMODE > 0){
            print_status();
          }
          if(pointer_array[input_var] == p_trial_set){
            tempTrialStautsMark = input_value;
          }else if(pointer_array[input_var] == p_OGActiveMills){
            OG_set();
          }else if(pointer_array[input_var] == p_miniscopeRecord){
            digitalWrite(MSRECORDPIN, input_value == 0? LOW: HIGH);
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
  SyncMark = 0;

  // pumpTimer = Timer.getAvailable(); 

  for(int i = 0; i < 8; i ++){
    readLickPins[i] = 23 + i*2;
    //attachInterrupt(digitalPinToInterrupt(readLickPins[i]), LickReportInInterrupt, RISING);
    waterServePins[i] = 22 + i*2;
    
    pinMode(waterServePins[i], OUTPUT);
    pinMode(readLickPins[i], INPUT);
    digitalWrite(readLickPins[i], LICK_SILENCE);
    digitalWrite(waterServePins[i], LOW);

  }

  // for(int i = 0; i < Length(readLickPins); i ++){
  //   attachInterrupt(digitalPinToInterrupt(readLickPins[i]), LickReportInInterrupt, RISING);
  // }
  pumpTimer.attachInterrupt(pump_set_call_by_interrupt);
  OGTimer.attachInterrupt(OG_set_call_by_interrupt);

  pinMode(13, OUTPUT);
  pinMode(50, OUTPUT);
  pinMode(OGOUTPIN, OUTPUT);
  pinMode(MSRECORDPIN, OUTPUT);
  pinMode(INFARREDDETCETPIN, INPUT);
  pinMode(PRESSLEVERPIN, INPUT);
  pinMode(SYNCINPUTPIN, INPUT);
  digitalWrite(13, LOW);
  digitalWrite(50, HIGH);
  digitalWrite(OGOUTPIN, LOW);
  digitalWrite(MSRECORDPIN, LOW);
  digitalWrite(INFARREDDETCETPIN, HIGH);
  digitalWrite(PRESSLEVERPIN, HIGH);
  digitalWrite(SYNCINPUTPIN, HIGH);
  

  attachInterrupt(digitalPinToInterrupt(readLickPins[0]), LickReportInInterrupt0, RISING);
  attachInterrupt(digitalPinToInterrupt(readLickPins[1]), LickReportInInterrupt1, RISING);
  // // attachInterrupt(digitalPinToInterrupt(readLickPins[2]), LickReportInInterrupt2, RISING);
  // // attachInterrupt(digitalPinToInterrupt(readLickPins[3]), LickReportInInterrupt3, RISING);
  // // attachInterrupt(digitalPinToInterrupt(readLickPins[4]), LickReportInInterrupt4, RISING);
  // // attachInterrupt(digitalPinToInterrupt(readLickPins[5]), LickReportInInterrupt5, RISING);
  // // attachInterrupt(digitalPinToInterrupt(readLickPins[6]), LickReportInInterrupt6, RISING);
  // // attachInterrupt(digitalPinToInterrupt(readLickPins[7]), LickReportInInterrupt7, RISING);

  // attachInterrupt(digitalPinToInterrupt(3), InfraRedInReportInInterrupt, FALLING);
  // attachInterrupt(digitalPinToInterrupt(5), InfraRedLeaveReportInInterrupt, RISING);
  // attachInterrupt(digitalPinToInterrupt(6), PressLeverReportInInterrupt, FALLING);
  attachInterrupt(digitalPinToInterrupt(SYNCINPUTPIN), Sync_call_by_interrupt, CHANGE);

  Serial.begin(250000);
  Serial.println();
  Serial.println("initialed:" + VERSION);
  Serial.println();
  SendDataBuffer.clear();
}

void loop() {
  
  while (!SendDataBuffer.isEmpty()) {
    // Serial.print("test ");
    // Serial.println(bufferGetString());
    serial_send(bufferGetString());
  }
  if (SyncMark) {
    // Serial.println(SyncMark);
    while(!serial_send("syncInfo:1")){};
    SyncMark = false;
  }

  if(tempTrialStautsMark != -1){//trial status update
    //serial_send("checking");
    // serial_send("debugLog:trialStatusMark="+String(tempTrialStautsMark));
    if(tempTrialStautsMark == 1){//start
      TrialStart();
      // serial_send("debugLog:trial start");

    }else { 
      if(tempTrialStautsMark == 2 && now_pos != -1){//serve water and end
        pump_set(waterServePins[now_pos], waterServeMicros[now_pos]);
        TrialEnd();
        // serial_send("debugLog:trial end");

      }
      else if(tempTrialStautsMark == 0){//end
        TrialEnd();
        // serial_send("debugLog:trial end, no water");
      }
    }
    tempTrialStautsMark = -1;
  }

  if(!waiting){

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
      if (inByte == 0x2f){
        if(!plainTextMark){plainTextMark = true;}
        else{
          // String tempCommand = Serial.readString();
          // Serial.println("received:" + tempCommand);
          // commandParse(tempCommand);
          commandParse(Serial.readString());
          plainTextMark = false;
          break;
        }
      }
    }


    // 如果在记录模式下
    if (isRecording) {
      if (inByte == 0xDD) {
        commandParse(ByteArrayToCommand(receivedData, indexInSerial));
        isRecording = false; // 结束记录
        indexInSerial = 0; // 重置索引
        break;
      }else if (inByte == 0xAA) {
        isRecording = true;
        indexInSerial = 0;
      }else{
        receivedData[indexInSerial++] = inByte; // 将字符添加到数组
        //Serial.print(inByte);
      }
      
      // 防止数组越界
      if (indexInSerial >= sizeof(receivedData)) {
        indexInSerial = 0;
        isRecording = false;
      }
    }
  }
}