#include <ADNS3080.h>
#include <MsTimer2.h>

#define lampPin 46    // pin that the pump is attached to
#define readPin 49 
#define waterPin 36    //change 40-36 ,原40 被用于2AFC ，响应仍是L pump
#define lickPin 30 
#define PIN_CS  23
#define PIN_RESET  25
#define airPin 2
#define analog_water_pin 2 
#define trial_count 3

int count_water = 0;
int enter_reward_context;             int* p_enter_reward_context = &enter_reward_context;
int in_reward_context = 0;            int* p_in_reward_context = &in_reward_context;
int lick_time_accu = 0;               int* p_lick_time_accu = &lick_time_accu;
int water_flush = 0;                  int* p_start_water = &water_flush;
int lick_count = 0;                   int* p_lick_count = &lick_count;
int lick_count_max=8;                 int* p_lick_count_max=&lick_count_max;
//unsigned long timelimit = 1000;
unsigned long previousMillis = 0;
long interval = 0;//舔和给水之间的间隔
unsigned long previousMillis3 = 0;
const long interval_samplerate = 10;//samplerate 10ms
unsigned long previousMillis4 = 0;
const int fixed_times = 50;
//int tr1 = 0;
int lick_mode=1;                  int* p_lick_mode = &lick_mode; //0：在奖励区域条件給水，1：随时根据条件給水
int lick_mode1_delay=0;           int* p_lick_mode1_delay = &lick_mode1_delay;//设定给水最低间隔,设为负数则停止舔舐给水，-2时给水并恢复至-1
int trial=0;                      int* p_trial = &trial;

String inputString = "";      // a String to hold incoming data
bool stringComplete = false;
int* pointer_array[]={p_enter_reward_context, p_in_reward_context, p_lick_time_accu, p_lick_count, p_start_water, p_lick_mode, p_trial, p_lick_count_max, p_lick_mode1_delay};
String serial_print_type[]={"move", "context_info", "received", "log"};

int active_pin=-1;

ADNS3080 <PIN_RESET, PIN_CS> sensor;

void pump_set(int pump_pin, int mills, bool write_mode=true){//write_mode: true:冲突则不设， false:冲突则覆盖
  if(pump_pin==active_pin && write_mode){return;}
  if(active_pin!=-1 && pump_pin!=active_pin){return;}//不允许当前timer在活动时设置其他针脚
  //Serial.println("timer started");
  digitalWrite(waterPin, HIGH);
  active_pin=pump_pin;
  MsTimer2::set(mills, pump_set_call_by_interrupt);
  MsTimer2::start();
}

void pump_set_call_by_interrupt(){
  digitalWrite(active_pin, LOW);
  active_pin=-1;
  MsTimer2::stop();
  //Serial.println("finished");
}

void init_by_PC(){
  int count_water = 0;
  int enter_reward_context;  
  int in_reward_context = 0; 
  int lick_time_accu = 0;    
  int water_flush = 0;       
  int lick_count = 0;        
  int lick_count_max=8;      
  unsigned long previousMillis = 0;
  long interval = 0;
  unsigned long previousMillis3 = 0;
  unsigned long previousMillis4 = 0;
  //int tr1 = 0;
  int lick_mode=1;           
  int lick_mode1_delay=0;    
  int trial=0;               
  digitalWrite(lampPin, HIGH);
  digitalWrite(airPin, LOW);
  digitalWrite(waterPin, LOW);
  digitalWrite(lickPin, LOW);
  digitalWrite(readPin, LOW);
  sensor.reset();
  sensor.setup();
  Serial.println("initialed");
}

void setup() {
  // initialize the LED pin as an output:
  pinMode(lampPin, INPUT);
  pinMode(readPin, INPUT);
  pinMode(waterPin, OUTPUT);
  pinMode(lickPin, OUTPUT);
  pinMode(airPin, OUTPUT);
  digitalWrite(lampPin, HIGH);
  digitalWrite(airPin, LOW);
  digitalWrite(waterPin, LOW);
  digitalWrite(lickPin, LOW);
  digitalWrite(readPin, LOW);
  //judge_l = digitalRead(lampPin);
  //enter_reward_context = digitalRead(lampPin);
  sensor.reset();
  sensor.setup();
  // initialize serial communications:
  Serial.begin(115200);
  Serial.println("initialed");
}

void loop() {
  int8_t dx, dy;      // Displacement since last function call
  dx=0; dy=0;
  sensor.displacement( &dx, &dy );
  dx+=128;dy+=128;
  if(dx!=0 || dy!=0){
    byte move_buffer[128];
    int temp_length = stringToByteArray("move:xy", move_buffer);
    move_buffer[5]=(byte)dx;
    move_buffer[6]=(byte)dy;
    serial_send(move_buffer, temp_length);
  }

  if (enter_reward_context>0) {
    lick_count=in_reward_context==0? lick_count_max: lick_count;//是否是刚进入奖励区域，0120lyf改
    trial=(trial+(in_reward_context==0? 1: 0));//当前一个场景context记一次trail
    in_reward_context = 1;
    previousMillis = millis();
  }
  else{
    lick_count=0;
    in_reward_context=0;
    //lick_time_accu=0;
  }
    
  unsigned long currentMillis3 = millis();

  if (currentMillis3 - previousMillis3 >= interval_samplerate) {//interval起每次监测舔的间隔作用//10ms采样一次
    if(digitalRead(readPin) == 0){lick_time_accu++; 
      //Serial.println("licked");
    }
    //lick_time_accu += digitalRead(readPin) == 0? 1: 0;
    digitalWrite(lickPin, digitalRead(readPin) == 0? HIGH: LOW);

    //tr1 += 1;
    //if (tr1>=9182){tr1=fixed_times+1;}
    if (lick_mode1_delay>0){lick_mode1_delay--;}
    previousMillis3 = currentMillis3;
    digitalWrite(waterPin, LOW);
    //start_water=0;//仅调试，后续删除
  }
  /////////////////////////////////////////////////////////////////////////////////判断舔水
  if(lick_mode==1){
    if(digitalRead(readPin)==0 && lick_mode1_delay==0){//只要舔就给水，加部分限制保证每次舔的量
      digitalWrite(waterPin, HIGH);
      //Serial.println("water_pin: high, readpin=1");
      digitalWrite(readPin, LOW);
      serial_send("context_info:lick:water_served");
      //Serial.println("");
      lick_mode1_delay=50;//设定给水最低有50*10ms间隔
    }
    if(lick_mode1_delay==-2){
      pump_set(waterPin, 5);
      lick_mode1_delay=-1;
    }
  }

  if (in_reward_context && lick_mode==0){
    unsigned long currentMillis = millis();
    if (currentMillis - previousMillis >= interval){  //interval=0表示不需要delay，到位置舔了直接给
      if (digitalRead(readPin)==1 && lick_time_accu>0) {//完成一次舔
        //tr1 = 0;                                      //初始化判断标准
        if ((lick_time_accu >= 1) && (lick_time_accu <= 50)){  //test lick, 50是多次舔的总时间，如果过长也没奖励
          //Serial.println("water_pin: reward zone"); // DQ
          lick_count -= 1; 
          if(lick_count>=0){
          pump_set(waterPin, 5);
          serial_send("context_info:lick:correct");
          serial_send("context_info:lick:water_served");
          //Serial.println(lick_count);
          }
          lick_time_accu = 0;
          serial_send("context_info:lick:");
        }
      }
    }
  }else if(!in_reward_context && lick_mode==0){
    unsigned long currentMillis = millis();
    if (currentMillis - previousMillis >= interval){  //interval=0表示不需要delay，到位置舔了直接给
      if (digitalRead(readPin)==1 && lick_time_accu>0) {//完成一次舔                                //初始化判断标准
        //if ((lick_time_accu >= 1) && (lick_time_accu <= 50)){
          serial_send("context_info:lick:");
          //Serial.println("context_info:lick:");
          lick_time_accu = 0;
        //}
      }
    }
  }

  if (stringComplete){
    stringComplete=0;
    String temp_log="received: "+inputString;
    Serial.println(temp_log);
    command_parse(inputString);
    inputString="";
  }

  if(water_flush==2){
    analogWrite(analog_water_pin, 255);
  }
  else{
    analogWrite(analog_water_pin, 0);
  }
}

void serialEvent() {
  while (Serial.available()) {
    char inChar = (char)Serial.read();
    inputString += inChar;
    if (inChar == '\n') {
      stringComplete = true;
    }
  }
}

void print_status(){
  //int* pointer_array[8]={p_judge_h, p_judge_l, p_current_place, p_lick_time_accu, p_lick_man, p_start_water, p_lick_mode, p_trial};
  char *log_buffer = new char[128];
  sprintf(log_buffer, "log: enter_zone %d, in_zone %d, lick_time_accu %d, lick_count %d, satrt_water %d, lick_mode %d, trial  %d", enter_reward_context, in_reward_context, lick_time_accu, lick_count, water_flush, lick_mode, trial);
  Serial.println(log_buffer);
  delete log_buffer;
}

void command_parse(String _command){
  if(_command == "check"){print_status();}
  else if(_command == "init") {init_by_PC();}
  else{
    Serial.print("pre:   ");
    print_status();
    int equal_pos=_command.indexOf('=');
    int input_var=(_command.substring(0, equal_pos)).toInt();
    int input_value=(_command.substring(equal_pos+1)).toInt();
    if(input_var>=0 && input_var<sizeof(pointer_array)){
      *(pointer_array[input_var])=input_value;
    }
    Serial.print("after: ");
    print_status();
  }
}

void serial_send(String inputStr){
  byte temp_buffer[255];
  int temp_length=stringToByteArray(inputStr, temp_buffer);
  Serial.write(temp_buffer, temp_length);
}

void serial_send(byte inputArr[], int _length){
  Serial.write(inputArr, _length);
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