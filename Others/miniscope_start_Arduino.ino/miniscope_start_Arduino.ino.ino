// These constants won't change:
//const int lampPin = 6;       // pin that the pump is attached to
const int readPin = 8;
//const int readPin_1 = 4;
const int testPin = 2;
//const int airPin = 12;
//unsigned long time_start = 0;
//unsigned long time_current = 0;
//unsigned long time_judge_stop = 0;
int count_start=1;
int lick_L=0;
int lick_H=0;
//int lick_L1=0;
//int lick_H1=0;
void setup() {
  // initialize the LED pin as an output:
//  pinMode(lampPin, OUTPUT); 
  pinMode(testPin, OUTPUT);
  //pinMode(airPin, OUTPUT); 
  pinMode(readPin, INPUT);
  //pinMode(readPin_1, INPUT);  
 // analogWrite(lampPin, 0);
  //digitalWrite(airPin,LOW);    
  digitalWrite(testPin,LOW);
  digitalWrite(readPin,LOW);
 // digitalWrite(readPin_1,LOW);
  // initialize serial communications:
  Serial.begin(115200);
  }

void loop() { 

    lick_L=digitalRead(readPin);
   // lick_L1=digitalRead(readPin_1);
    delay(0);
    lick_H=digitalRead(readPin);
    //lick_H1=digitalRead(readPin_1);
    if((lick_L==1) && (lick_H==1)&&(count_start==1))
    {     
    digitalWrite(testPin,HIGH);//测试引脚
    /*analogWrite(lampPin, 250);
    delay(50);
    analogWrite(lampPin, 0);    
    digitalWrite(testPin,LOW);//测试引脚*/
    lick_L=0;
    lick_H=0;
    count_start=count_start-1;
    } 
    /*if((lick_L1==1) && (lick_H1==1))
    {     
    digitalWrite(testPin,HIGH);//测试引脚
    digitalWrite(airPin,HIGH);
    delay(300);
    digitalWrite(airPin,LOW);   
    digitalWrite(testPin,LOW);//测试引脚 
    lick_L1=0;
    lick_H1=0;
    } */
}
