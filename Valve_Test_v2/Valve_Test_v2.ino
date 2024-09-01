#define MEGA false

#if MEGA
int TOUT[]={22, 24, 25, 27};
int TIN[]={10, 33, 35, 37};
int ManualOncePin = 21;
int ManualStartPin = 20;
#else
int TOUT[]={4, 5, 6};
int TIN[]={7, 8, 9};
int ManualOncePin = 3;
int ManualStartPin = 2;
#endif

bool waiting = false;
bool waterServeAtOnce = false;
bool bRunning = false;
float serveMillis[] = {20, 20, 0, 0};
int TINStatus[] = {0, 0, 0, 0};
int iTN = 100;
int i=0;
int ready = -1;
int pinNum = 0;
int delayBetweenPin = 50;
int delayMillSec = 200;
int delayLongMillSec = 1000;

void StartWater(){
  bRunning = true;
  i=0;
  Serial.println("start");
}

void StartWaterAtOnce(){
  waterServeAtOnce = true;
}

void setup() {
  pinNum = sizeof(TOUT)/sizeof(TOUT[0]);
  
  //pinMode(ManualOncePin, INPUT_PULLUP);
  //pinMode(ManualStartPin, INPUT_PULLUP);
  //digitalWrite(ManualStartPin, INPUT_PULLUP);
  // put your setup code here, to run once:
  for(int i = 0; i < pinNum; i++){
    pinMode(TOUT[i], OUTPUT);
    pinMode(TIN[i], INPUT);
    digitalWrite(TOUT[i], LOW);
    digitalWrite(TIN[i], HIGH);
  }

  bRunning = false;
  #if MEGA
  attachInterrupt(2, StartWaterAtOnce, FALLING);
  attachInterrupt(3, StartWater, FALLING);
  digitalWrite(21, HIGH);
  digitalWrite(20, HIGH);
  #else
  attachInterrupt(0, StartWaterAtOnce, FALLING);
  attachInterrupt(1, StartWater, FALLING);
  digitalWrite(2, HIGH);
  digitalWrite(3, HIGH);
  #endif

  Serial.begin(115200);
  Serial.println("initialed");
}

void loop() {
  if(waterServeAtOnce){
    if(!waiting){
      waterServeAtOnce = false;
      waiting = true;
      Serial.println("watering in manual once");
      for(int i = 0; i < pinNum; i++){
        digitalWrite(TOUT[i], HIGH);
        delay(serveMillis[i]);
        digitalWrite(TOUT[i], LOW);
        delay(delayBetweenPin);
      }
      delay(delayLongMillSec);
      waiting = false;
    }
  }

  for(int i = 0; i < pinNum; i++){//手动指定pin单次
    if(digitalRead(TIN[i]) == LOW){
      if(TINStatus[i] == 0){
        if(serveMillis[i] > 0){
          Serial.println("watering in manual once");
          digitalWrite(TOUT[i], HIGH);
          delay(serveMillis[i]);
          digitalWrite(TOUT[i], LOW);
          delay(delayBetweenPin);
        }
      }
      TINStatus[i] = 1;
    }else {
      TINStatus[i] = 0;
    }
  }

  // if(digitalRead(ManualOncePin) == LOW){//手动全部单次
  //   ready = 1;
  // }
  // else{
  //   if(ready == 1){
  //     ready = -1;
  //     for(int i = 0; i < pinNum; i++){
  //       Serial.println("watering in manual");
  //       digitalWrite(TOUT[i], HIGH);
  //       delay(serveMillis[i]);
  //       digitalWrite(TOUT[i], LOW);
  //     }
  //     delay(delayMillSec);
  //   }
  // }

  if(bRunning&&i<iTN){
    Serial.print("watering");
    Serial.println(i);
    for(int j = 0; j < pinNum; j++){
      digitalWrite(TOUT[j], HIGH);
      delay(serveMillis[j]);
      digitalWrite(TOUT[j], LOW);
      delay(delayBetweenPin);

    }
    delay(delayMillSec);
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
