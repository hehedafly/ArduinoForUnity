void setup() {
  // put your setup code here, to run once:
  pinMode(14, INPUT);
  pinMode(15, INPUT);
  Serial.begin(9600);
}

void loop() {
  // put your main code here, to run repeatedly:
  
  int data = analogRead(15);
  if(data!=0){
    Serial.print("ttl: ");
    Serial.print(data);
    Serial.print(" ;  ");
    data = analogRead(14);
    Serial.print("out: ");
    Serial.println(data);

  }
  delay(20);
}
