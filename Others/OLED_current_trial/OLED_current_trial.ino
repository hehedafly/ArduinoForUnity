#include "U8glib.h"                           //加载显示库文件
U8GLIB_SSD1306_128X64 u8g(13, 11, 10, 9, 8);  // SW SPI Com: SCK = 13, MOSI = 11, CS = 10, A0 = 9 RST = 8
const int readPin = 2;
const int readPin1 = 4;
int judge_l;
int judge_h;
int judge_l1;
int judge_h1;
int Trial_current = 0;
int Trial_initial = 1;
void setup() {
  pinMode(readPin, INPUT);
  pinMode(readPin1, INPUT);
  digitalWrite(readPin, LOW);
  digitalWrite(readPin1, LOW);
  judge_l = digitalRead(readPin);
  judge_h = digitalRead(readPin);
  judge_l1 = digitalRead(readPin1);
  judge_h1 = digitalRead(readPin1);
  Trial_current = 0;
  Trial_initial = 1;
  u8g.setFont(u8g_font_fub20);  //设置字体和自号，目前测试字号有fub14,17,20,30
  Serial.begin(115200);
}
void loop() {
  judge_h = digitalRead(readPin);
  judge_h1 = digitalRead(readPin1);
  delay(200);
  judge_h = digitalRead(readPin);
  judge_h1 = digitalRead(readPin1);
  if (Trial_initial) {  //是否是刚进入奖励区域
    Serial.println("initial");
    Trial_initial = 0;
    Trial_current = 0;
    u8g.firstPage();  //一下是显示实现部分
    do {
      u8g.setPrintPos(20, 50);  //显示的位置
      u8g.print("Initial");     //显示Trial字样
    } while (u8g.nextPage());
    delay(100);  //显示的时间间隔。
  }
  if ((judge_h == 1) && (judge_l == 0)){  //是否是刚进入奖励区域
    judge_l = judge_h;
    Trial_current += 1;
    char *log_trial=new char[32];
    sprintf(log_trial, "now_trial: %d, and h, h1, l, l1: %d, %d, %d, %d", Trial_current, judge_h, judge_h1, judge_l, judge_l1);
    Serial.println(log_trial);
    u8g.firstPage();  //一下是显示实现部分
    do {
      u8g.setPrintPos(25, 50);   //显示的位置
      u8g.print(Trial_current);  //显示变量i的值
      u8g.setPrintPos(85, 50);   //显示的位置
      u8g.print("Tr");           //显示Trial字样
    } while (u8g.nextPage());
    delay(100);  //显示的时间间隔。
  }
  if ((judge_h1 == 1) && (judge_l1 == 0)){  //是否是刚进入奖励区域
  
    judge_l1 = judge_h1;
    Trial_current += 1;
    char *log_trial=new char[32];
    sprintf(log_trial, "now_trial: %d, and h, h1, l, l1: %d, %d, %d, %d", Trial_current, judge_h, judge_h1, judge_l, judge_l1);
    Serial.println(log_trial);
    u8g.firstPage();  //一下是显示实现部分
    do {
      //u8g.setFont(u8g_font_fub30);//设置字体和自号，目前测试字号有fub14,17,20,30
      u8g.setPrintPos(25, 50);   //显示的位置
      u8g.print(Trial_current);  //显示变量i的值
      u8g.setPrintPos(85, 50);   //显示的位置
      u8g.print("Tr");           //显示Trial字样
    } while (u8g.nextPage());
    delay(100);  //显示的时间间隔。
  }
  if ((judge_h == 0) && (judge_l == 1)) {
    judge_l = judge_h;
  }
  if ((judge_h1 == 0) && (judge_l1 == 1)) {
    judge_l1 = judge_h1;
  }
}
