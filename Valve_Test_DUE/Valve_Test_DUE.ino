#include <DueTimer.h>

int waterServePins[8];
int ManualOncePin = 21;

bool waiting = false;
bool waitingBetweenPins = false;
bool waterServeAtOnce = false;
bool bRunning = false;
int waterServeMicros[8] = {30000, 30000, 30000, 30000, 30000, 30000, 30000, 30000};      int* p_waterServeMicros = waterServeMicros;
int TINStatus[] = {0, 0, 0, 0};
int iTN = 100;
int i=0;
int ready = -1;
int pinNum = 0;
int delayBetweenPin = 50;
int delayMillSec = 100;
int delayLongMillSec = 1000;

DueTimer pumpTimer0 = Timer.getAvailable();
DueTimer pumpTimer1 = Timer.getAvailable();
DueTimer pumpTimer2 = Timer.getAvailable();
DueTimer pumpTimer3 = Timer.getAvailable();
DueTimer pumpTimer4 = Timer.getAvailable();
DueTimer pumpTimer5 = Timer.getAvailable();
DueTimer pumpTimer6 = Timer.getAvailable();
DueTimer pumpTimer7 = Timer.getAvailable();

void pump0_set_call_by_interrupt(){digitalWrite(waterServePins[0], LOW);pumpTimer0.stop();}
void pump1_set_call_by_interrupt(){digitalWrite(waterServePins[1], LOW);pumpTimer1.stop();}
void pump2_set_call_by_interrupt(){digitalWrite(waterServePins[2], LOW);pumpTimer2.stop();}
void pump3_set_call_by_interrupt(){digitalWrite(waterServePins[3], LOW);pumpTimer3.stop();}
void pump4_set_call_by_interrupt(){digitalWrite(waterServePins[4], LOW);pumpTimer4.stop();}
void pump5_set_call_by_interrupt(){digitalWrite(waterServePins[5], LOW);pumpTimer5.stop();}
void pump6_set_call_by_interrupt(){digitalWrite(waterServePins[6], LOW);pumpTimer6.stop();}
void pump7_set_call_by_interrupt(){digitalWrite(waterServePins[7], LOW);pumpTimer7.stop();}

void pump_set(){//write_mode: true:冲突则不设， false:冲突则覆盖
  for(int i = 0; i < 8; i ++){
    digitalWrite(waterServePins[i], HIGH);
  }
  pumpTimer0.start(waterServeMicros[0]);
  pumpTimer1.start(waterServeMicros[1]);
  pumpTimer2.start(waterServeMicros[2]);
  pumpTimer3.start(waterServeMicros[3]);
  pumpTimer4.start(waterServeMicros[4]);
  pumpTimer5.start(waterServeMicros[5]);
  pumpTimer6.start(waterServeMicros[6]);
  pumpTimer7.start(waterServeMicros[7]);
}


void StartWater(){
  bRunning = true;
  i=0;
  Serial.println("start");
}

void StartWaterAtOnce(){
  waterServeAtOnce = true;
}

void setup() {
  for(int i = 0; i < 8; i ++){
    waterServePins[i] = 22 + i*2;
  }
  for(int i = 0; i < pinNum; i++){
    pinMode(waterServePins[i], OUTPUT);
    digitalWrite(waterServePins[i], LOW);
  }

  bRunning = false;
  pumpTimer0.attachInterrupt(pump0_set_call_by_interrupt);
  pumpTimer1.attachInterrupt(pump1_set_call_by_interrupt);
  pumpTimer2.attachInterrupt(pump2_set_call_by_interrupt);
  pumpTimer3.attachInterrupt(pump3_set_call_by_interrupt);
  pumpTimer4.attachInterrupt(pump4_set_call_by_interrupt);
  pumpTimer5.attachInterrupt(pump5_set_call_by_interrupt);
  pumpTimer6.attachInterrupt(pump6_set_call_by_interrupt);
  pumpTimer7.attachInterrupt(pump7_set_call_by_interrupt);

  Serial.begin(115200);
  Serial.println("initialed");
}

void loop() {
  if(waterServeAtOnce){
    if(!waiting){
      waterServeAtOnce = false;
      waiting = true;
      Serial.println("watering in manual once");
      pump_set();
      delay(delayLongMillSec);
      waiting = false;
    }
  }

  if(bRunning&&i<iTN){
    Serial.print("watering");
    Serial.println(i);
    pump_set();
    delay(delayLongMillSec);
    i++;
    if(i == iTN){bRunning =false;Serial.println("end");}
  }
  // put your main code here, to run repeatedly:
}

void serialEvent() {
  char inChar = (char)Serial.read();
  switch (inChar) {
    case 'W':
    case 'w':
      {
        bRunning = true;
        i=0;
        Serial.println("start");
      }
      break;

    case 'e':
    case 'E':
      {
        bRunning = false;
        i=0;
        Serial.println("end");
      }
      break;

    case 'o':
    case 'O':
      {
        StartWaterAtOnce();
      }

    default:
    break;
  }
}
