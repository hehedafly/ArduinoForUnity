//Written by Saintgene<saintgene@gmail.com> 2015
//Please contact Saintgene for the permission of redistrubition.

#include <DueTimer.h>
#define PIDC 13
#define MSG_SZ 12 //is not equal to sizeof(Msg)
#define INTERVAL 20 //20us 50 kHz

struct Msg
{
  uint64_t TimeStamp;
  uint32_t EventCode;
};

volatile Msg msg;
volatile uint64_t tTime = 0;
volatile uint32_t EventCode_Pre = 0xffffffff;
volatile uint32_t EventCode_Cur = 0xffffffff;
bool bRunning = false;

void myTimerHandler()
{
  tTime++;
  EventCode_Cur = REG_PIOC_PDSR;
  if (EventCode_Pre != EventCode_Cur)
  {
    EventCode_Pre = EventCode_Cur;
    msg.TimeStamp = tTime;
    msg.EventCode = EventCode_Cur;
    Serial.write((byte *)(&msg), MSG_SZ);
  }
}


void setup() {
  REG_PMC_PCER0 = 0x1 << PIDC; //enable clock for PortC
  REG_PIOC_PER = 0xffffffff; //set PortC controlled by PIO controller
  REG_PIOC_ODR = 0xffffffff; // set PortC input mode
  REG_PIOC_PUER = 0xffffffff; //pull up PortC
  Serial.begin(115200);
  Timer1.attachInterrupt(myTimerHandler);
  Timer1.setPeriod(INTERVAL); //20 us, 50k Hz
}

void loop() {
  SerialUSBEvent();
}

void SerialUSBEvent()
{
  if (Serial.available() > 0)
  { char inChar = (char)Serial.read();

    switch (inChar)
    {
      case 'S':
      case 's':
        {
          EventCode_Pre = 0xffffffff;
          EventCode_Cur = 0xffffffff;
          tTime = 0;
          bRunning = true;
          Timer1.start();
        }
        break;

      case 'E':
      case 'e':
        {
          Timer1.stop();
          tTime = 0;
          if (bRunning)
          {
            msg.TimeStamp = 0x0;
            msg.EventCode = 0x0;
            Serial.write((byte *)(&msg), MSG_SZ);
            bRunning = false;
          }
        }
        break;

      //    case 't':
      //    case 'T':
      //      {
      //        SerialUSB.println(sizeof(Msg));
      //        SerialUSB.println(sizeof(uint32_t));
      //        SerialUSB.println(sizeof(uint64_t));
      //      }
      //      break;

      default:
        break;

    }

    while (Serial.available() > 0)
    {
      Serial.read();
    }
  }
}
