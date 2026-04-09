#include <DueTimer.h>
#include <RingBuf.h>
#include <algorithm>

// ========== 通讯方式配置 ==========
// 设置为 1 使用 USB 通讯 (SerialUSB)，设置为 0 使用串口通讯 (Serial)
#define USE_USB_SERIAL 1

// 根据配置选择串口类型
#if USE_USB_SERIAL
  #define CUSTOM_SERIAL SerialUSB
#else
  #define CUSTOM_SERIAL Serial
#endif

int waterServePins[8];//22,24,26...36
int readLickPins[8];//23,25,27...37
int waterServeMicros[8] = {30000, 30000, 30000, 30000, 30000, 30000, 30000, 30000};      int* p_waterServeMicros = waterServeMicros;

const int INFARREDDETCETPIN = 49;
const int PRESSLEVERPIN = 48;
const int OGOUTPIN = 40;
const int MSRECORDPIN = 41;
const int SYNCINPUTPIN = 42;

const int REACH = 1;
const int LEAVE = 0;

String VERSION = "V2.3";
// const int SINGNALOUTPINS[8];
// const int SINGNALINPUTPINS[8];

int LICK_ACTIVE = HIGH;
int LICK_SILENCE = LOW;

int waiting = 1;                      //int* p_waiting = &waiting;
int lick_mode = 0;                    int* p_lick_mode = &lick_mode;
int trial=0;                          int* p_trial = &trial;
int trial_set = 0;                    int* p_trial_set = &trial_set;//设为0时结束，设为1时开始, 设为2时按now_pos给水
int now_pos = -1;                     int* p_now_pos = &now_pos;//只管给水，不管屏幕显示
int pre_pos = -1;
int lick_rec_pos = -1;                int* p_lick_rec_pos = &lick_rec_pos;
int water_flush[8] = {};              int* p_water_flush = water_flush;
int lick_count[8] = {};               int* p_lick_count = lick_count;
int OGActiveMills = 100;              int* p_OGActiveMills = &OGActiveMills;
int miniscopeRecord = 0;              int* p_miniscopeRecord = &miniscopeRecord;
int waterServeWhenLick = 0;           int* p_waterServeWhenLick = &waterServeWhenLick;
int waterServeManual = -1;             int* p_waterServeManual = &waterServeManual;

int INDEBUGMODE = 0;                  int* p_INDEBUGMODE = &INDEBUGMODE;

int tempLickPos = -1;
int tempTrialStautsMark = -1;
int lickEndThresholdMills = -1;

byte receivedData[512];
// RingBuf<char, 2048> SendDataBuffer;
// RingBuf<char, 2048> SendDataBufferSec;
// volatile RingBuf<String, 128> SyncDataBuffer;
volatile bool SyncMark = false;
int indexInSerial = 0;
bool isRecording = false;
bool plainTextMark = false;
int maxMsgCountPerChunk = 20;

// ========== 握手状态 ==========
bool handshakeDone = false;           // 全局握手状态标志
unsigned long lastHandshakeAttempt = 0;  // 上次握手尝试时间
//                        0           1            2             3            4             5           6           7                   8                             9      
int* pointer_array[]={p_lick_mode, p_trial, p_trial_set, p_now_pos, p_lick_rec_pos, p_INDEBUGMODE, p_OGActiveMills, p_miniscopeRecord, p_waterServeWhenLick, p_waterServeManual};
int* pointerArrayType_array[]={p_waterServeMicros, p_lick_count, p_water_flush};
int  pointerArrayType_arrayLength[]={8, 8};

//                                  0         1          2           3            4      5          6             7          8          9       10              11
// const char* serial_print_type[]={"lick", "entrance", "press", "context_info", "log", "echo", "value_change", "command", "debugLog", "stay", "syncInfo", "miniscopeStart"};
const char* serial_print_type[]={"li",      "en",       "pr",     "ci",          "log", "echo", "vc",           "cmd",     "debugLog", "st",    "si",       "ms"};

// ========== 缓存配置 ==========
// 在此数组中添加需要缓存的前缀
const char* cachePrefixes[] = {"li", "en", "pr"};
const int CACHE_PREFIX_COUNT = sizeof(cachePrefixes) / sizeof(cachePrefixes[0]);  // 自动计算前缀数量
const int MAX_CACHE_ENTRIES = 64;  // 最大缓存条目数
const int CACHE_STRING_MAX_LEN = 24;

// 缓存结构体
struct CacheEntry {
  char key[CACHE_STRING_MAX_LEN];
  byte value[256];
  int valueLength;
  bool valid;
};

// 缓存数组
CacheEntry liCache[MAX_CACHE_ENTRIES];
int cacheEntryCount = 0;

// 查找缓存
bool findInCache(const char* key, byte* outputArr, int& outLength) {
  for (int i = 0; i < cacheEntryCount; i++) {
    if (liCache[i].valid && strcmp(liCache[i].key, key) == 0) {
      memcpy(outputArr, liCache[i].value, liCache[i].valueLength);
      outLength = liCache[i].valueLength;
      return true;
    }
  }
  return false;
}

// 添加到缓存
void addToCache(const char* key, const byte* value, int length) {
  // 检查是否已存在
  for (int i = 0; i < cacheEntryCount; i++) {
    if (liCache[i].valid && strcmp(liCache[i].key, key) == 0) {
      // 更新已存在的条目
      liCache[i].valueLength = length;
      memcpy(liCache[i].value, value, length);
      return;
    }
  }

  // 如果缓存未满，添加新条目
  if (cacheEntryCount < MAX_CACHE_ENTRIES) {
    strncpy(liCache[cacheEntryCount].key, key, CACHE_STRING_MAX_LEN - 1);
    liCache[cacheEntryCount].key[CACHE_STRING_MAX_LEN - 1] = '\0';
    liCache[cacheEntryCount].valueLength = length;
    memcpy(liCache[cacheEntryCount].value, value, length);
    liCache[cacheEntryCount].valid = true;
    cacheEntryCount++;
  }
}

// 检查字符串是否以指定前缀开头
bool startsWith(const char* str, const char* prefix) {
  return strncmp(str, prefix, strlen(prefix)) == 0;
}

// 检查字符串是否匹配任意需要缓存的前缀
bool matchesCachePrefix(const char* str) {
  for (int i = 0; i < CACHE_PREFIX_COUNT; i++) {
    if (startsWith(str, cachePrefixes[i])) {
      return true;
    }
  }
  return false;
}

DueTimer pumpTimer = Timer.getAvailable();
DueTimer OGTimer = Timer.getAvailable();
volatile int active_pin=-1;

class DoubleBuffer {
private:
    RingBuf<char, 1024> bufferA;
    RingBuf<char, 1024> bufferB;
    volatile RingBuf<char, 1024>* volatile writeBuffer;
    volatile RingBuf<char, 1024>* volatile readBuffer;
    volatile bool swapRequested;
    
public:
    DoubleBuffer() : writeBuffer(&bufferA), readBuffer(&bufferB), swapRequested(false) {}
    
    bool push(const char* msg) {
        noInterrupts();
        RingBuf<char, 1024>* currentWrite = (RingBuf<char, 1024>*)writeBuffer;
        interrupts();
        
        size_t len = strlen(msg);
        len = std::min(len, (size_t)0xff);
        int requiredSpace = len + 2;
        
        if (currentWrite->size() + requiredSpace > currentWrite->maxSize() * 0.8) {
            swapRequested = true;  // 达到80%容量时请求交换
        }
        
        if (currentWrite->size() + requiredSpace > currentWrite->maxSize()) {
            return false;
        }
        
        currentWrite->push((char)(len + 1));
        for (size_t i = 0; i < len; i++) {
            currentWrite->push(msg[i]);
        }
        currentWrite->push((char)0);
        
        return true;
    }
    
    bool shouldSwap() const { return swapRequested; }
    
    void swap() {
        noInterrupts();
        volatile RingBuf<char, 1024>* temp = writeBuffer;
        writeBuffer = readBuffer;
        readBuffer = temp;
        ((RingBuf<char, 1024>*)writeBuffer)->clear();
        swapRequested = false;
        interrupts();
    }
    
    RingBuf<char, 1024>* getReadBuffer() { return (RingBuf<char, 1024>*)readBuffer; }
    RingBuf<char, 1024>* getWriteBuffer() { return (RingBuf<char, 1024>*)writeBuffer; }
};

DoubleBuffer sendDataBuffers;

template<class T>
int Length(T& arr){
  return sizeof(arr) / sizeof(arr[0]);
}

void SerialLog(String inputStr, String input_head=""){//"xxx"
  byte temp_buffer[256];
  int temp_length=stringToByteArray("log:"+input_head+inputStr, temp_buffer);
  CUSTOM_SERIAL.write(temp_buffer, temp_length);
}

bool serial_sendRaw(byte inputArr[], int _length){
  size_t wl = CUSTOM_SERIAL.write(inputArr, _length);
  CUSTOM_SERIAL.println();
  return wl == _length;
}

bool serial_send(String inputStr){//"context_info:xxx" or ...
  byte temp_buffer[256];
  int temp_length = 0;

  // 检查是否匹配需要缓存的前缀
  if (matchesCachePrefix(inputStr.c_str())) {
    if (findInCache(inputStr.c_str(), temp_buffer, temp_length)) {
      // 缓存命中，直接发送
      return serial_sendRaw(temp_buffer, temp_length);
    }
    // 缓存未命中，进行转换并缓存
    temp_length = stringToByteArray(inputStr, temp_buffer);
    addToCache(inputStr.c_str(), temp_buffer, temp_length);
    return serial_sendRaw(temp_buffer, temp_length);
  }

  // 非缓存前缀，正常转换
  temp_length = stringToByteArray(inputStr, temp_buffer);
  return serial_sendRaw(temp_buffer, temp_length);
}

void pump_set(int pump_pin, int micros, bool write_mode=true){//write_mode: true:冲突则不设， false:冲突则覆盖
  if(pump_pin==active_pin && write_mode){
    //serial_send("log:pump set while pumping");
    return;
  }
  if(active_pin!=-1 && pump_pin!=active_pin){return;}//不允许当前timer在活动时设置其他针脚
  if(INDEBUGMODE > 0){
    char _content[sizeof("debugLog:pump set at x lasting xxxx") + 8] = "";
    sprintf(_content, "debugLog:pump set at %d lasting %d", pump_pin, micros);
    bufferPushChar(_content);
    // serial_send("debugLog:pump set at "+String(pump_pin)+" lasting "+String(micros));
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

void ExecuteWhenLicking(int lickSpout){
  // if(waterServeWhenLick == 1 & now_pos != -1){
  //   pump_set(lickSpout, waterServeMicros[now_pos]);
  // }
}


void LickReportInInterrupt0(){      char entry[16];sprintf(entry, "%s:%d:%d", serial_print_type[0], 0, REACH);bufferPushChar(entry);lick_count[0]++;ExecuteWhenLicking(0);return;}
void LickReportInInterrupt1(){      char entry[16];sprintf(entry, "%s:%d:%d", serial_print_type[0], 1, REACH);bufferPushChar(entry);lick_count[1]++;ExecuteWhenLicking(1);return;}
void LickReportInInterrupt2(){      char entry[16];sprintf(entry, "%s:%d:%d", serial_print_type[0], 2, REACH);bufferPushChar(entry);lick_count[2]++;ExecuteWhenLicking(2);return;}
void LickReportInInterrupt3(){      char entry[16];sprintf(entry, "%s:%d:%d", serial_print_type[0], 3, REACH);bufferPushChar(entry);lick_count[3]++;ExecuteWhenLicking(3);return;}
void LickReportInInterrupt4(){      char entry[16];sprintf(entry, "%s:%d:%d", serial_print_type[0], 4, REACH);bufferPushChar(entry);lick_count[4]++;ExecuteWhenLicking(4);return;}
void LickReportInInterrupt5(){      char entry[16];sprintf(entry, "%s:%d:%d", serial_print_type[0], 5, REACH);bufferPushChar(entry);lick_count[5]++;ExecuteWhenLicking(5);return;}
void LickReportInInterrupt6(){      char entry[16];sprintf(entry, "%s:%d:%d", serial_print_type[0], 6, REACH);bufferPushChar(entry);lick_count[6]++;ExecuteWhenLicking(6);return;}
void LickReportInInterrupt7(){      char entry[16];sprintf(entry, "%s:%d:%d", serial_print_type[0], 7, REACH);bufferPushChar(entry);lick_count[7]++;ExecuteWhenLicking(7);return;}

void LickReportInInterrupt0_leave(){char entry[16];sprintf(entry, "%s:%d:%d", serial_print_type[0], 0, LEAVE);bufferPushChar(entry);lick_count[0]++;return;}

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
  return bufferPushChar(strmsg.c_str());
}

bool bufferPushChar(const char* msg) {
    return sendDataBuffers.push(msg);
}

String bufferGetString(RingBuf<char, 1024>* buffer) {
  char _len;
  if (!buffer->pop(_len)) {
      return "";
  }
  
  int len = (int)_len;
  
  // 快速合理性检查
  if (len <= 0 || len > 100) {
      // 损坏数据，清空缓冲区
      buffer->clear();
      return "";
  }
  
  if (buffer->size() < len - 1) {
      // 数据不完整，恢复长度字节
      buffer->push(_len);
      return "";
  }
  
  // 检查结束符
  char _end;
  buffer->peek(_end, len - 1);
  if (_end != 0) {
      // 损坏消息，丢弃
      for (int i = 0; i < len; i++) {
          char _t;
          buffer->pop(_t);
      }
      return "";
  }
  
  // 读取有效消息
  char data[len];
  for (int i = 0; i < len; i++) {
      buffer->pop(data[i]);
  }
  
  data[len - 1] = '\0';
  return String(data);
}

void print_status(String _head=""){
  // //                          0           1          2           3             4             5           6           7                   8                      9      
  // int* pointer_array[]={p_lick_mode, p_trial, p_trial_set, p_now_pos, p_lick_rec_pos, p_INDEBUGMODE};
  char *log_buffer = new char[512];
  sprintf(log_buffer, " lick_mode %d, trial %d, tempTrialStautsMark %d, now_pos %d INDEBUGMODE %d waiting %d" ,
                        lick_mode, trial, tempTrialStautsMark, now_pos, INDEBUGMODE, waiting);
  String temp_log = "debugLog:"+ _head+ log_buffer;
  
  serial_send(temp_log);
  delete log_buffer;
}

void print_ArrayStatus(String _head=""){
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

  temp_log += "; water_flush: ";
  for(int i =0; i < 8; i++){
    temp_log += String(water_flush[i]);
    if(i<7){temp_log += ", ";}
  }
  temp_log = "debugLog:"+ _head+ temp_log;
  serial_send(temp_log);
}

size_t stringToByteArray(String inputStr, byte* outputArray) {//return full length of byte_array
  String typeStr = inputStr.substring(0, inputStr.indexOf(':'));
  String contentStr = inputStr.substring(inputStr.indexOf(':') + 1);
  uint16_t typeId = -1;
  for (int i = 0; i < (sizeof(serial_print_type) / sizeof(serial_print_type[0])); i++) {
    if (strcmp(typeStr.c_str(), serial_print_type[i]) == 0) {
        typeId = i;
        break;
    }
  }

  outputArray[0] = 0xAA;
  outputArray[1] = typeId & 0xFF;

  // 计算内容长度
  size_t contentLength = std::min((unsigned int)0xff, contentStr.length());
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
    CUSTOM_SERIAL.print("length mismatch: ");
    CUSTOM_SERIAL.print(temp_type);
    CUSTOM_SERIAL.print(" ");
    CUSTOM_SERIAL.print(temp_length);
    CUSTOM_SERIAL.print(" ");
    CUSTOM_SERIAL.print(arraySize);
    CUSTOM_SERIAL.print(" ");
    CUSTOM_SERIAL.println();
    return "";}
  else{
    char res[temp_length + 1];
    memcpy(res, byte_array+2, temp_length);
    res[temp_length] = '\0';
    return res;
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
  waterServeWhenLick = 0;
  //print_status("In trial end:");
  return 1;
}

void init_by_PC(bool resetConnectStatus = true){
  // === 状态变量 ===
  waiting = 1;
  lick_mode = 0;
  trial = 0;
  trial_set = 0;
  now_pos = -1;
  pre_pos = -1;
  lick_rec_pos = -1;
  tempTrialStautsMark = -1;
  waterServeWhenLick = 0;
  waterServeManual = -1;
  OGActiveMills = 100;
  miniscopeRecord = 0;
  INDEBUGMODE = 0;
  tempLickPos = -1;
  lickEndThresholdMills = -1;

  // === 计数器清零 ===
  std::fill(std::begin(water_flush), std::end(water_flush), 0);
  std::fill(std::begin(lick_count), std::end(lick_count), 0);

  // === 停止所有Timer ===
  pumpTimer.stop();
  OGTimer.stop();
  active_pin = -1;
  digitalWrite(OGOUTPIN, LOW);

  // === 输出引脚复位 ===
  for(int i = 0; i < 8; i ++){
    digitalWrite(waterServePins[i], LOW);
  }
  digitalWrite(13, LOW);
  digitalWrite(MSRECORDPIN, LOW);

  // === 通信状态复位 ===
  isRecording = false;
  indexInSerial = 0;
  plainTextMark = false;
  SyncMark = false;

  // === 发送缓冲区清空 ===
  // 交换到空缓冲区
  if (sendDataBuffers.shouldSwap()) {
    sendDataBuffers.swap();
  }

  if(resetConnectStatus){
    handshakeDone = false;
  }
  // serial_send("initialed manullay");
}

void commandParse(String _command){

  _command.replace("\n", "");
  _command.replace("\r", "");
  int equal_pos=_command.indexOf('=');
  if(_command.compareTo("check") == 0){
    print_status();
    return;
  }
  else if(_command.compareTo("checkArray")==0){
    print_ArrayStatus();
    return;
  }
  else if(_command.compareTo("forceinit")==0){
    init_by_PC();
    return;
  }
  else if(_command.compareTo("clear")==0){
    init_by_PC(false);
    return;
  }

  if(equal_pos>0 && _command.substring(0, equal_pos).compareTo("sw")==0){
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
            // serial_send("log:invalid message!");
            return;
          }
          if(pointerArrayType_array[input_ArrVar] == water_flush){
            water_flush[input_ArrInd] = (input_value >= 1? (water_flush[input_ArrInd] > 0? 0: 1): 0);
            digitalWrite(waterServePins[input_ArrInd], (water_flush[input_ArrInd] > 0? HIGH: LOW));
            // bufferPushString("water_flush set" + String(input_ArrInd) + " to " + String(waterServePins[input_ArrInd]));
          }else{
            *(pointerArrayType_array[input_ArrVar] + input_ArrInd)=input_value;
            // bufferPushString("array" + String(input_ArrVar)+ " set" + String(input_ArrInd) + " to " + String(waterServePins[input_ArrInd]));
          }
          char echoContent[256] = "";
          sprintf(echoContent, "echo:%s:echo", _command.c_str());
          CUSTOM_SERIAL.println(echoContent);
          // serial_send("echo:"+_command+":echo");
          if(INDEBUGMODE > 0){
            // CUSTOM_SERIAL.flush();
            print_ArrayStatus();
            // CUSTOM_SERIAL.flush();
          }
        }else{
          // serial_send("log:invalid message!");
          return;
        }
      }else{// normal variable change
        // serial_send("debugLog:in parse:<"+_command+">");

        int input_var=(_command.substring(0, equal_pos)).toInt();
        int input_value=(_command.substring(equal_pos+1)).toInt();

        if(input_var>=0 && input_var<sizeof(pointer_array)){
          *(pointer_array[input_var])=input_value;
          char echoContent[256] = "";
          sprintf(echoContent, "echo:%s:echo", _command.c_str());
          CUSTOM_SERIAL.println(echoContent);
          if(INDEBUGMODE > 0){
            print_status();
          }
          if(pointer_array[input_var] == p_trial_set){
            tempTrialStautsMark = input_value;
          }else if(pointer_array[input_var] == p_OGActiveMills){
            OG_set();
          }else if(pointer_array[input_var] == p_miniscopeRecord){
            digitalWrite(MSRECORDPIN, input_value == 0? LOW: HIGH);
          }else if(pointer_array[input_var] == p_now_pos){
            pre_pos = now_pos;
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
  // waterserving = 0;
  SyncMark = 0;
  std::fill(std::begin(water_flush), std::end(water_flush), 0);
  std::fill(std::begin(lick_count), std::end(lick_count), 0);                                    

  for(int i = 0; i < 8; i ++){
    readLickPins[i] = 22 + i*2;
    waterServePins[i] = 23 + i*2;
    
    pinMode(waterServePins[i], OUTPUT);
    pinMode(readLickPins[i], INPUT);
    digitalWrite(readLickPins[i], HIGH);
    // digitalWrite(readLickPins[i], LICK_SILENCE);
    digitalWrite(waterServePins[i], LOW);

  }

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
  attachInterrupt(digitalPinToInterrupt(readLickPins[1]), LickReportInInterrupt0_leave, FALLING);
  // // attachInterrupt(digitalPinToInterrupt(readLickPins[2]), LickReportInInterrupt2, RISING);
  // // attachInterrupt(digitalPinToInterrupt(readLickPins[3]), LickReportInInterrupt3, RISING);
  // // attachInterrupt(digitalPinToInterrupt(readLickPins[4]), LickReportInInterrupt4, RISING);
  // // attachInterrupt(digitalPinToInterrupt(readLickPins[5]), LickReportInInterrupt5, RISING);
  // // attachInterrupt(digitalPinToInterrupt(readLickPins[6]), LickReportInInterrupt6, RISING);
  // // attachInterrupt(digitalPinToInterrupt(readLickPins[7]), LickReportInInterrupt7, RISING);

  // attachInterrupt(digitalPinToInterrupt(3), InfraRedInReportInInterrupt, FALLING);
  // attachInterrupt(digitalPinToInterrupt(5), InfraRedLeaveReportInInterrupt, RISING);
  // attachInterrupt(digitalPinToInterrupt(6), PressLeverReportInInterrupt, FALLING);
  // attachInterrupt(digitalPinToInterrupt(SYNCINPUTPIN), Sync_call_by_interrupt, CHANGE);

  #if USE_USB_SERIAL
    CUSTOM_SERIAL.begin(115200);
  #else
    CUSTOM_SERIAL.begin(250000);
  #endif

  // 握手循环：不断发送初始化消息直到收到 ACK
  handshakeDone = false;
  while (!handshakeDone) {
    CUSTOM_SERIAL.println("initialed:" + VERSION);

    unsigned long startTime = millis();
    String cmd = "";
    while (!handshakeDone && (millis() - startTime < 500)) {
      while (CUSTOM_SERIAL.available() > 0) {
        char c = CUSTOM_SERIAL.read();
        if (c == '\n' || c == '\r') {
          cmd.replace("\r", "");
          cmd.replace("\n", "");
          if (cmd.endsWith("ACK")) {
            handshakeDone = true;  // 收到确认，退出握手循环
            CUSTOM_SERIAL.println("ACK_OK");  // 确认握手成功
          }
          cmd = "";
        } else {
          cmd += c;
        }
      }
      delay(10);
    }
    delay(100);
  }

}

void loop() {

  while (!handshakeDone) {
    CUSTOM_SERIAL.println("initialed:" + VERSION);

    unsigned long startTime = millis();
    String cmd = "";
    while (!handshakeDone && (millis() - startTime < 500)) {
      while (CUSTOM_SERIAL.available() > 0) {
        char c = CUSTOM_SERIAL.read();
        if (c == '\n' || c == '\r') {
          cmd.replace("\r", "");
          cmd.replace("\n", "");
          if (cmd.endsWith("ACK")) {
            handshakeDone = true;  // 收到确认，退出握手循环
            CUSTOM_SERIAL.println("ACK_OK");  // 确认握手成功
          }
          cmd = "";
        } else {
          cmd += c;
        }
      }
      delay(10);
    }
    delay(100);
  }

  if (sendDataBuffers.shouldSwap()) {
      sendDataBuffers.swap();
  }
  
  // 处理读取缓冲区
  RingBuf<char, 1024>* readBuffer = sendDataBuffers.getReadBuffer();
  int _count = 0;
  
  while (!readBuffer->isEmpty() && _count < maxMsgCountPerChunk) {
      String message = bufferGetString(readBuffer);
      if (message.length() > 0) {
          serial_send(message);
          _count++;
      }
  }
  
  // 如果读取缓冲区空了且写入缓冲区有数据，主动交换
  if (readBuffer->isEmpty() && !sendDataBuffers.getWriteBuffer()->isEmpty()) {
      sendDataBuffers.swap();
  }

  if(tempTrialStautsMark != -1){//trial status update
    if(tempTrialStautsMark == 1){//start
      TrialStart();

    }else { 
      if(tempTrialStautsMark == 2 && now_pos != -1){//serve water and end
        pump_set(waterServePins[now_pos], waterServeMicros[now_pos]);
        TrialEnd();
        // serial_send("debugLog:trial end");

      }
      else if (tempTrialStautsMark == 3 && now_pos != -1) {
        waterServeWhenLick = 1;
      }
      else if(tempTrialStautsMark == 0){//end
        TrialEnd();
        // serial_send("debugLog:trial end, no water");
      }
    }
    tempTrialStautsMark = -1;
  }

  if(waterServeManual != -1){
    if(waterServeManual == -2){
      if(pre_pos != -1){waterServeManual = pre_pos;}
    }

    if(waterServeManual > -1 & waterServeManual < Length(waterServePins)){
      pump_set(waterServePins[waterServeManual], waterServeMicros[waterServeManual]);
    }
    waterServeManual = -1;
  }

  if(!waiting){

    if(lick_mode==0){
      //不用舔就出水
    }
    else if(lick_mode==1){
      //舔了才出水
    }
  }

  while (CUSTOM_SERIAL.available()) { // 当有数据可读时
    byte inByte = (byte)CUSTOM_SERIAL.read();

    // 检查是否接收到起始标志
    if (!isRecording && inByte == 0xAA) {
      isRecording = true;
    }else if (!isRecording) {
      if (inByte == 0x2f){
        if(!plainTextMark){plainTextMark = true;}
        else{
          // String tempCommand = CUSTOM_SERIAL.readString();
          // CUSTOM_SERIAL.println("received:" + tempCommand);
          // commandParse(tempCommand);
          commandParse(CUSTOM_SERIAL.readString());
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
        // CUSTOM_SERIAL.print(inByte);
      }

      // 防止数组越界
      if (indexInSerial >= sizeof(receivedData)) {
        indexInSerial = 0;
        isRecording = false;
      }
    }
  }
}