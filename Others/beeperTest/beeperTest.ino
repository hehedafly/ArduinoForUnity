#include <TimerOne.h>

byte receivedData[256];
int index = 0;
bool isRecording = false;
int pwmFreq = 100;                    int* p_pwmFreq = &pwmFreq;
int* pointer_array[]={p_pwmFreq};

void setup() {
  // put your setup code here, to run once:
  pinMode(2, OUTPUT);
  Timer1.initialize(500000); //The led will blink in a half second time interval
  //Timer1.attachInterrupt(blinkLed);
  Serial.begin(115200);
}

void command_parse(String _command){
  // // Serial.print("in parse: ");
  // // Serial.print(_command);
  // Serial.println();
  //_command.replace("\r", "");
  // if(_command.compareTo("check")==0){print_status();}
  // else if(_command.compareTo("init")==0) {init_by_PC();}
  // else{
    int equal_pos=_command.indexOf('=');
    int arrayStartIndc=_command.indexOf('[');//format: 1[2]=0
    int arrayEndIndc = _command.indexOf(']');
    if(equal_pos>0){
      if(arrayStartIndc > 0){//array type variable change
        if(arrayEndIndc > 0){
          int input_ArrVar=(_command.substring(0, arrayStartIndc)).toInt();
          int input_ArrInd=(_command.substring(arrayStartIndc+1, arrayEndIndc)).toInt();
          int input_value=(_command.substring(equal_pos+1)).toInt();
          // if(input_ArrInd >= pointerArrayType_arrayLength[input_ArrVar]){
          //   //serial_send("log:invalid message!");
          //   return;
          // }
          //*(pointerArrayType_array[input_ArrVar]+sizeof(int)*input_ArrInd)=input_value;
          Serial.println("echo:"+_command+":echo");
          //serial_send("echo:"+_command+":echo");
        }else{
          //serial_send("log:invalid message!");
          return;
        }
      }else{// normal variable change
        //serial_send("log:in parse:"+_command);

        int input_var=(_command.substring(0, equal_pos)).toInt();
        int input_value=(_command.substring(equal_pos+1)).toInt();
        if(input_var>=0 && input_var<sizeof(pointer_array)){
          *(pointer_array[input_var])=input_value;
          Serial.println("echo:"+_command+":echo");
          //serial_send("echo:"+_command+":echo");
          //pump_set(23, 100);
        }
      }
    //}
  }
}

void loop() {

  while (Serial.available()) { // 当有数据可读时
    command_parse(Serial.readline());
  }
}


