//#include <MsTimer2.h>

// These constants won't change:
//3,5,6,11被timer0, timer2影响
//const int readPinA = 10;      //主控36连的是这个
//const int readPin_1 = 4;
const int testPin = 2;
const int manual_pin = 5;
const int WaterPin = 6;       // waterL连到pcb的6
const int readPin = 11;  
const int airPin = 12;      //waterR
const int demarcatePin = 13;
unsigned long time_start = 0;
unsigned long time_current = 0;
unsigned long time_judge_stop = 0;
unsigned long previousMillis = 0;
unsigned long nowMillis = 0;

int serve_milis = 66;             int* p_serve_milis = &serve_milis;
int waterSignal=1;                 int* p_lickSignal = &waterSignal;
// int lick_H=0;
//int lick_H_rec=lick_H;
// int lick_L1=0;
// int lick_H1=0;
int lickA_L=0;
// int lickA_H=0;
int water_supply_mode=0;      int* p_water_supply_mode = &water_supply_mode;
//int mode_lock=0;
int temp_signal=0;
int pin_arr[] = {WaterPin};
int active_pin = -1;//lampPin
bool demarcating = false;
int demarcateTimes = 0;

String inputString = "";      // a String to hold incoming data
bool stringComplete = false;
int* pointer_array[]={p_lickSignal, p_serve_milis, p_water_supply_mode};

// void pump_set(int pump_pin_pos, int mills, bool write_mode=true){//write_mode: true:冲突则不设， false:冲突则覆盖
//   if(pump_pin_pos==active_pin && write_mode){return;}
//   if(active_pin!=-1 && pin_arr[pump_pin_pos]!=active_pin){return;}//不允许当前timer在活动时设置其他针脚
//   digitalWrite(pin_arr[pump_pin_pos],HIGH);
//   digitalWrite(testPin,HIGH);
//   active_pin=pin_arr[pump_pin_pos];
//   MsTimer2::set(mills, pump_set_call_by_interrupt);
//   MsTimer2::start();
//   Serial.println("timer started");
// }

// void pump_set_call_by_interrupt(){
//   digitalWrite(active_pin,LOW);
//   digitalWrite(testPin,LOW);
//   active_pin=-1;
//   MsTimer2::stop();
//   Serial.println("finished");
// }
void pump_set(int _serve_milis){
  serve_milis = _serve_milis;
  nowMillis=millis();
  previousMillis=nowMillis;
  digitalWrite(WaterPin,HIGH);
  digitalWrite(testPin,HIGH);
  waterSignal=1;
}

void print_status(){
  char *log_buffer = new char[64];
  sprintf(log_buffer, "log: --serve_millis %d, --WS_mode %d", serve_milis, water_supply_mode);
  Serial.println(log_buffer);
  delete log_buffer;
}

void command_parse(String _command){
  if(_command == "check" || _command=="check\n"){print_status();}
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

void setup() {
  // initialize the LED pin as an output:
  pinMode(WaterPin, OUTPUT); 
  pinMode(testPin, OUTPUT);
  pinMode(readPin, INPUT);
  //pinMode(readPinA, INPUT);
  //pinMode(readPin_1, INPUT); 
  pinMode(manual_pin, INPUT);
  pinMode(demarcatePin, INPUT);
  //pinMode(time_test_writepin, OUTPUT);
  //pinMode(time_test_readpin, INPUT);
  digitalWrite(13, LOW);
  digitalWrite(WaterPin, LOW);
  digitalWrite(testPin,LOW);
  digitalWrite(manual_pin, HIGH);
  digitalWrite(readPin, HIGH);
  digitalWrite(demarcatePin, HIGH);
  Serial.begin(9600);
  Serial.println("initialed");
}

void loop() { 
    while (Serial.available()) {
    char inChar = (char)Serial.read();
    if (inChar == '\n') {
      stringComplete = true;
      break;
    }
    inputString += inChar;
  }

  if(digitalRead(demarcatePin) == LOW && !demarcating){
    demarcating = true;
    demarcateTimes = 100;
  }
  while (demarcating && demarcateTimes) {
    digitalWrite(WaterPin,HIGH);
    delay(serve_milis);
    digitalWrite(WaterPin,LOW);
    delay(5);
    demarcateTimes--;
    Serial.print("now:");
    Serial.println(demarcateTimes);
    if(demarcateTimes == 0){demarcating = false;break;}
  }

  waterSignal=digitalRead(readPin);

  if (inputString != "" && stringComplete){
    stringComplete = false;
    Serial.print("string received: ");
    Serial.println(inputString);
    command_parse(inputString);
    inputString="";
  }

  temp_signal=digitalRead(manual_pin);
  //Serial.println(temp_signal);
  if(temp_signal==LOW){
    water_supply_mode=1;
  }else{
    water_supply_mode=0;
    digitalWrite(testPin,LOW);
  }

  if (water_supply_mode==1){//持续给水
    digitalWrite(testPin,HIGH);//测试引脚
    Serial.println("water supplying in mode1");
    digitalWrite(WaterPin,HIGH);
    delay(100);
    digitalWrite(WaterPin,LOW);
    delay(5);
  }
  else if (water_supply_mode==0){
    //digitalWrite(lampPin, LOW);    
    //digitalWrite(testPin,LOW);//测试引脚
    if(waterSignal==0 && nowMillis==0){
      pump_set(serve_milis);
      Serial.println("water supplied in mode 0");
      //pump_set(0, serve_milis);
      //lickA_L=0;
    } 

  }
  if(nowMillis!=0){
    //Serial.println("watering");
    nowMillis=millis();
    if(nowMillis-previousMillis>=serve_milis){
      digitalWrite(testPin,LOW);
      digitalWrite(WaterPin,LOW);
      nowMillis=0;
    }
  }
}

// void serialEvent() {
// }
