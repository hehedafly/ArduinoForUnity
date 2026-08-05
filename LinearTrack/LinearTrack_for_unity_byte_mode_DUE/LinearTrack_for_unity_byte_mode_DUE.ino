#include <DueTimer.h>
#include <RingBuf.h>
#include <algorithm>

// =====================================================================
// LinearTrack 单块 Arduino Due 固件
// 由两块 AVR Arduino 合并而来：
//   - VR_lick_for_unity_byte_mode（运动编码器 + 舔水检测 + 奖励给水 + ws 上报）
//   - samll_arduino_new_Lroom_A（手动给水按钮 + demarcate 标定 + 持续给水）
// 结构仿照 LEDRing_for_unity_byte_mode_NewVersion_V2_4（握手 / DueTimer /
// 字节帧协议 / //forceinit 明文 / DoubleBuffer 双缓冲）。
// 协议索引与变量表顺序与 Unity 端 LinearTrackMoving.cs 的 lsTypes /
// Arduino_var_list 完全一致，勿改顺序。
// =====================================================================

// ========== 通讯方式配置 ==========
// 设置为 1 使用 USB 通讯 (SerialUSB, Native 口)，设置为 0 使用串口通讯 (Serial, Programming 口)
#define USE_USB_SERIAL 1
#if USE_USB_SERIAL
  #define CUSTOM_SERIAL SerialUSB
#else
  #define CUSTOM_SERIAL Serial
#endif

String VERSION = "LT1.0";  // 版本号，Unity config.ini serialSettings/compatibleVersion 需匹配（或留空）

// ========== 引脚表（Due 重新设计，用户按此接线）==========
// 运动编码器（跑球/转轮，两通道正交）
#define ENCODER_INT_PIN    2     // 中断触发（原 VR_lick pin2）
#define ENCODER_A_PIN      3     // 通道A（原 pin23）
#define ENCODER_B_PIN      4     // 通道B（原 pin25）
// 舔水检测 / 奖励给水 / 指示
#define LICK_READ_PIN      49    // 舔水信号输入
#define WATER_SERVE_PIN    36    // 奖励水阀
#define LICK_INDICATE_PIN  30    // 舔水指示LED
// 手动 / 标定（整合自 small arduino）
#define MANUAL_WATER_PIN   5     // 手动给水按钮（按下持续给水）
#define DEMARCATE_PIN      6     // 标定按钮（触发 100 次定量脉冲）
#define TEST_PIN           13    // 板载LED / 示波器同步

// 水阀极性（沿用 VR_lick 的低电平有效，用户可翻转）
#define WATER_ACTIVE       LOW
#define WATER_IDLE         HIGH

// ========== 舔水电平定义 ==========
#define LICK_REVERSE false  // touch panel 导通为1；默认反相：导通为0不导通为1
#if LICK_REVERSE
  const int LICK_ACTIVE  = 1;
  const int LICK_SILENCE = 0;
#else
  const int LICK_ACTIVE  = 0;
  const int LICK_SILENCE = 1;
#endif

// ========== 实验变量（顺序 = Unity Arduino_var_list，下标 0-9）==========
int enter_reward_context = 0;    int* p_enter_reward_context = &enter_reward_context;
int in_reward_context = 0;       int* p_in_reward_context = &in_reward_context;
int lick_time_accu = 0;          int* p_lick_time_accu = &lick_time_accu;
int lick_count = 0;              int* p_lick_count = &lick_count;
int water_flush = 0;             int* p_start_water = &water_flush;
int lick_mode = 0;               int* p_lick_mode = &lick_mode;      // 0:奖励区按条件给水，1:随时按条件给水
int trial = 0;                   int* p_trial = &trial;
int lick_count_max = 0;          int* p_lick_count_max = &lick_count_max;
int lick_mode0_delay = 0;        int* p_lick_mode0_delay = &lick_mode0_delay;
int lick_mode1_delay = 0;        int* p_lick_mode1_delay = &lick_mode1_delay; // 普通延时变量（与 -1/正数并列，不再驱动出水）
int serveWaterReward = 0;        int* p_serveWaterReward = &serveWaterReward; // Unity 的“奖励出水”命令：置 1 出水一次后归零

//                        0                       1                    2                 3              4               5           6         7                  8                    9                   10
int* pointer_array[] = {p_enter_reward_context, p_in_reward_context, p_lick_time_accu, p_lick_count, p_start_water, p_lick_mode, p_trial, p_lick_count_max, p_lick_mode0_delay, p_lick_mode1_delay, p_serveWaterReward};
const int POINTER_ARRAY_COUNT = sizeof(pointer_array) / sizeof(pointer_array[0]);

//                          0        1               2       3        4               5
// 顺序 = Unity lsTypes 位置：move / context_info / log / echo / value_change / command（仅数组下标参与传输）
String serial_print_type[] = {"move", "context_info", "log", "echo", "value_change", "command"};

// ========== 给水 / 标定参数 ==========
int serveMicros = 5000;         // 奖励给水脉宽(us)，原 5ms
int demarcateMillis = 66;       // 标定单次脉宽(ms)，原 small arduino serve_milis
int manualPulseMillis = 100;    // 手动持续给水单次脉宽(ms)

// ========== 采样 / 计时 ==========
const long interval_samplerate = 10;   // 10ms 采样一次
unsigned long previousMillis  = 0;      // 进入奖励区时刻
unsigned long previousMillis3 = 0;      // 采样计时
long interval = 0;                      // 舔和给水之间的间隔

// ========== 运动编码器 ==========
volatile long encoderLength = 0;

// ========== 手动 / 标定状态 ==========
bool demarcating = false;
int  demarcateTimes = 0;

// ========== 串口接收 ==========
byte receivedData[256];
int  indexInSerial = 0;
bool isRecording = false;
bool plainTextMark = false;

// ========== 握手 ==========
bool handshakeDone = false;

// ========== 发送节流 ==========
int maxMsgCountPerChunk = 20;

// ========== 定时器 ==========
DueTimer pumpTimer = Timer.getAvailable();
volatile int active_pin = -1;

// =====================================================================
// DoubleBuffer 双缓冲（移植自 V2.4）：所有文本上报的统一非阻塞出口
// =====================================================================
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
            swapRequested = true;  // 达到80%容量请求交换
        }
        if (currentWrite->size() + requiredSpace > currentWrite->maxSize()) {
            return false;          // 满则丢弃
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

// =====================================================================
// 字节协议 / 发送
// =====================================================================
size_t stringToByteArray(String inputStr, byte* outputArray) {  // 返回字节数组全长
    String typeStr = inputStr.substring(0, inputStr.indexOf(':'));
    String contentStr = inputStr.substring(inputStr.indexOf(':') + 1);

    uint16_t typeId = 0xFFFF;
    for (unsigned int i = 0; i < sizeof(serial_print_type) / sizeof(serial_print_type[0]); i++) {
        if (typeStr.equals(serial_print_type[i])) { typeId = i; break; }
    }

    outputArray[0] = 0xAA;
    outputArray[1] = typeId & 0xFF;

    size_t contentLength = contentStr.length();
    if (contentLength > 0xff) { contentLength = 0xff; }
    outputArray[2] = contentLength & 0xFF;
    // 以下 index 均减2（对齐 V2.4 / VR_lick）
    for (size_t i = 0; i < contentLength; ++i) {
        outputArray[3 + i] = (byte)contentStr[i];
    }
    outputArray[3 + contentLength] = 0xDD;
    return 4 + contentLength;
}

String ByteArrayToCommand(byte byte_array[], int arraySize) {  // 无前后补位，不转 type 为文字
    // type: 1byte   length: 1byte   content: ...
    int temp_type = (int)byte_array[0];
    int temp_length = byte_array[1];

    if (arraySize - 2 != temp_length) {
        CUSTOM_SERIAL.print("length mismatch: ");
        CUSTOM_SERIAL.print(temp_type);   CUSTOM_SERIAL.print(" ");
        CUSTOM_SERIAL.print(temp_length); CUSTOM_SERIAL.print(" ");
        CUSTOM_SERIAL.print(arraySize);   CUSTOM_SERIAL.println();
        return "";
    } else {
        char res[temp_length + 1];
        memcpy(res, byte_array + 2, temp_length);
        res[temp_length] = '\0';
        return String(res);
    }
}

bool serial_sendRaw(byte inputArr[], int _length) {
    size_t wl = CUSTOM_SERIAL.write(inputArr, _length);
    CUSTOM_SERIAL.println();
    return wl == (size_t)_length;
}

void serial_send(String inputStr) {  // "context_info:xxx" or "log:xxx" ...
    byte temp_buffer[256];
    int temp_length = stringToByteArray(inputStr, temp_buffer);
    serial_sendRaw(temp_buffer, temp_length);
}

bool bufferPushChar(const char* msg) {   // ISR/主循环均可入队
    return sendDataBuffers.push(msg);
}
bool bufferPushString(String strmsg) {
    return bufferPushChar(strmsg.c_str());
}

String bufferGetString(RingBuf<char, 1024>* buffer) {
    char _len;
    if (!buffer->pop(_len)) { return ""; }
    int len = (int)_len;

    if (len <= 0 || len > 100) {   // 损坏数据，清空
        buffer->clear();
        return "";
    }
    if (buffer->size() < len - 1) {  // 数据不完整，恢复长度字节
        buffer->push(_len);
        return "";
    }
    char _end;
    buffer->peek(_end, len - 1);
    if (_end != 0) {                 // 损坏消息，丢弃
        for (int i = 0; i < len; i++) { char _t; buffer->pop(_t); }
        return "";
    }
    char data[len];
    for (int i = 0; i < len; i++) { buffer->pop(data[i]); }
    data[len - 1] = '\0';
    return String(data);
}

void SerialLog(String inputStr, String input_head = "") {
    bufferPushString("log:" + input_head + inputStr);
}

// =====================================================================
// 给水（MsTimer2 -> DueTimer）
// =====================================================================
void pump_set_call_by_interrupt() {
    digitalWrite(active_pin, WATER_IDLE);
    active_pin = -1;
    digitalWrite(TEST_PIN, LOW);
    pumpTimer.stop();
}

void pump_set(int pump_pin, int micros, bool write_mode = true) { // write_mode: true 冲突则不设，false 冲突则覆盖
    if (pump_pin == active_pin && write_mode) { return; }
    if (active_pin != -1 && pump_pin != active_pin) { return; }   // 不允许当前 timer 活动时设置其他针脚
    digitalWrite(pump_pin, WATER_ACTIVE);
    digitalWrite(TEST_PIN, HIGH);
    active_pin = pump_pin;
    bufferPushChar("context_info:ws");        // 单板后由此处上报 ws，经双缓冲发送
    pumpTimer.attachInterrupt(pump_set_call_by_interrupt).start(micros);
}

// 直接阻塞脉冲（手动/标定用，不上报 ws，避免刷屏）
void pulseWaterDirect(int ms) {
    if (active_pin != -1) { return; }         // 实验给水进行中则跳过
    digitalWrite(WATER_SERVE_PIN, WATER_ACTIVE);
    digitalWrite(TEST_PIN, HIGH);
    delay(ms);
    digitalWrite(WATER_SERVE_PIN, WATER_IDLE);
    digitalWrite(TEST_PIN, LOW);
}

// =====================================================================
// 运动编码器 ISR
// =====================================================================
void CalcLengthCallByInterrupt() {
    encoderLength += digitalRead(ENCODER_A_PIN) - digitalRead(ENCODER_B_PIN);
}

// =====================================================================
// 状态 / 初始化 / 命令
// =====================================================================
void print_status(String _head = "") {
    char *log_buffer = new char[256];
    sprintf(log_buffer, " enter_zone %d, in_zone %d, lick_time_accu %d, lick_count %d, start_water %d, lick_mode %d, trial %d, lick_count_max %d, lick_mode0_delay %d, lick_mode1_delay %d",
            enter_reward_context, in_reward_context, lick_time_accu, lick_count, water_flush, lick_mode, trial, lick_count_max, lick_mode0_delay, lick_mode1_delay);
    bufferPushString("log:" + _head + log_buffer);
    delete[] log_buffer;
}

void init_by_PC(bool resetConnectStatus = true) {
    // === 实验变量清零 ===
    enter_reward_context = 0;
    in_reward_context = 0;
    lick_time_accu = 0;
    water_flush = 0;
    lick_count = 0;
    lick_count_max = 0;
    lick_mode = 0;
    lick_mode0_delay = 0;
    lick_mode1_delay = 0;
    serveWaterReward = 0;
    trial = 0;

    // === 计时复位 ===
    previousMillis = 0;
    previousMillis3 = 0;

    // === 停止给水 / 输出复位 ===
    pumpTimer.stop();
    active_pin = -1;
    digitalWrite(WATER_SERVE_PIN, WATER_IDLE);
    digitalWrite(LICK_INDICATE_PIN, LOW);
    digitalWrite(TEST_PIN, LOW);

    // === 手动/标定复位 ===
    demarcating = false;
    demarcateTimes = 0;

    // === 通信状态复位 ===
    isRecording = false;
    indexInSerial = 0;
    plainTextMark = false;

    // === 发送缓冲清空 ===
    if (sendDataBuffers.shouldSwap()) { sendDataBuffers.swap(); }

    if (resetConnectStatus) { handshakeDone = false; }
    SerialLog("initialed");
}

void commandParse(String _command) {
    _command.replace("\n", "");
    _command.replace("\r", "");

    if (_command.compareTo("check") == 0)      { print_status(); return; }
    else if (_command.compareTo("forceinit") == 0) { init_by_PC(true);  return; }
    else if (_command.compareTo("clear") == 0)     { init_by_PC(false); return; }
    else if (_command.compareTo("ping") == 0)      { return; }  // 保活，忽略

    int equal_pos = _command.indexOf('=');
    if (equal_pos > 0) {   // 普通变量修改：<idx>=<val>
        int input_var = (_command.substring(0, equal_pos)).toInt();
        int input_value = (_command.substring(equal_pos + 1)).toInt();
        if (input_var >= 0 && input_var < POINTER_ARRAY_COUNT) {
            *(pointer_array[input_var]) = input_value;
            char echoContent[256] = "";
            sprintf(echoContent, "echo:%s:echo", _command.c_str());
            CUSTOM_SERIAL.println(echoContent);   // 明文 echo 供 Unity Context_verify 的 ReadLine 校验
            bufferPushChar(echoContent);          // 字节帧 echo 供 Unity 异步 commandVerifyDict 清除（进入/离开奖励区 0=1/0=-1）
        }
    }
}

// =====================================================================
// 握手（对齐 V2.4 与 Unity CreateSerialConnection）
// =====================================================================
void doHandshake() {
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
                        handshakeDone = true;             // 收到确认，退出握手循环
                        CUSTOM_SERIAL.println("ACK_OK");  // 回复握手成功
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

// =====================================================================
// setup
// =====================================================================
void setup() {
    // 运动编码器
    pinMode(ENCODER_INT_PIN, INPUT);
    pinMode(ENCODER_A_PIN, INPUT);
    pinMode(ENCODER_B_PIN, INPUT);
    digitalWrite(ENCODER_A_PIN, HIGH);
    digitalWrite(ENCODER_B_PIN, HIGH);

    // 舔水 / 给水 / 指示
    pinMode(LICK_READ_PIN, INPUT);
    pinMode(WATER_SERVE_PIN, OUTPUT);
    pinMode(LICK_INDICATE_PIN, OUTPUT);
    digitalWrite(WATER_SERVE_PIN, WATER_IDLE);
    digitalWrite(LICK_INDICATE_PIN, LOW);

    // 手动 / 标定
    pinMode(MANUAL_WATER_PIN, INPUT);
    pinMode(DEMARCATE_PIN, INPUT);
    digitalWrite(MANUAL_WATER_PIN, HIGH);
    digitalWrite(DEMARCATE_PIN, HIGH);

    pinMode(TEST_PIN, OUTPUT);
    digitalWrite(TEST_PIN, LOW);

    pumpTimer.attachInterrupt(pump_set_call_by_interrupt);
    attachInterrupt(digitalPinToInterrupt(ENCODER_INT_PIN), CalcLengthCallByInterrupt, RISING);

    CUSTOM_SERIAL.begin(115200);

    // 握手：不断发送 initialed:VERSION 直到收到 ACK
    handshakeDone = false;
    doHandshake();
}

// =====================================================================
// loop
// =====================================================================
void loop() {
    // 握手保持（掉线/forceinit 后重新握手）
    if (!handshakeDone) { doHandshake(); }

    // ---- 排空发送双缓冲 ----
    if (sendDataBuffers.shouldSwap()) { sendDataBuffers.swap(); }
    RingBuf<char, 1024>* readBuffer = sendDataBuffers.getReadBuffer();
    int _count = 0;
    while (!readBuffer->isEmpty() && _count < maxMsgCountPerChunk && CUSTOM_SERIAL.availableForWrite() > 5) {
        String message = bufferGetString(readBuffer);
        if (message.length() > 0) { serial_send(message); _count++; }
    }
    if (readBuffer->isEmpty() && !sendDataBuffers.getWriteBuffer()->isEmpty()) {
        sendDataBuffers.swap();
    }

    // ---- 运动上报（二进制帧，直接发送，绕开字符串缓冲）----
    if (encoderLength != 0) {
        if (abs(encoderLength) < 16) {
            byte move_buffer[32];
            int temp_length = stringToByteArray("move:xy", move_buffer);
            move_buffer[temp_length - 3] = (byte)(encoderLength + 64);  // dx+64
            move_buffer[temp_length - 2] = (byte)64;                    // dy=0 -> +64
            serial_sendRaw(move_buffer, temp_length);
        }
        encoderLength = 0;
    }

    // ---- 进出奖励区 ----
    if (enter_reward_context > 0) {
        lick_count = in_reward_context == 0 ? lick_count_max : lick_count;
        in_reward_context = 1;
        previousMillis = millis();
        enter_reward_context = 0;
    } else if (enter_reward_context == -1) {
        enter_reward_context = 0;
        in_reward_context = 0;
        lick_count = 0;
    }

    // ---- 10ms 采样：累计舔、指示、递减 delay ----
    unsigned long currentMillis3 = millis();
    if (currentMillis3 - previousMillis3 >= interval_samplerate) {
        if (digitalRead(LICK_READ_PIN) == LICK_ACTIVE) { lick_time_accu++; }
        digitalWrite(LICK_INDICATE_PIN, digitalRead(LICK_READ_PIN) == LICK_ACTIVE ? HIGH : LOW);
        if (lick_mode1_delay > 0) { lick_mode1_delay--; }
        if (lick_mode0_delay > 0) { lick_mode0_delay--; }
        previousMillis3 = currentMillis3;
    }

    // ---- 舔水判定：给水判定已上移 Unity，Arduino 仅上报每次完成的舔 ----
    // 完成一次舔 = 采样期检测到有舔(lick_time_accu>0) 后回到静默电平
    if (digitalRead(LICK_READ_PIN) == LICK_SILENCE && lick_time_accu > 0) {
        lick_time_accu = 0;
        bufferPushChar("context_info:lick:");   // 只上报，不判定正确性、不自动出水
    }
    // 原 lick_mode==0/1 的自动出水与 correct 判定逻辑已删除（判定移至 Unity LickingCheck）：
    //   mode0: in_reward_context 内 lick_count 递减达标则 pump_set + "lick:correct"
    //   mode1: lick_mode1_delay==0 时上报 correct；lick_mode1_delay==-2 时 pump_set
    // 保留以便追溯。lick_mode/lick_mode0_delay/lick_mode1_delay 仅作数值同步，不再驱动出水。

    // ---- Unity 奖励出水命令（mode 无关）----
    if (serveWaterReward == 1) {
        pump_set(WATER_SERVE_PIN, serveMicros);
        serveWaterReward = 0;
    }

    // ---- flush 给水 ----
    if (water_flush == 2) {
        pump_set(WATER_SERVE_PIN, serveMicros);
    }

    // ---- 手动给水按钮（整合自 small arduino）：按下持续给水 ----
    if (digitalRead(MANUAL_WATER_PIN) == LOW && active_pin == -1) {
        pulseWaterDirect(manualPulseMillis);
        delay(5);
    }

    // ---- demarcate 标定（整合自 small arduino）：100 次定量脉冲 ----
    if (digitalRead(DEMARCATE_PIN) == LOW && !demarcating) {
        demarcating = true;
        demarcateTimes = 100;
    }
    while (demarcating && demarcateTimes) {
        pulseWaterDirect(demarcateMillis);
        delay(5);
        demarcateTimes--;
        CUSTOM_SERIAL.print("now:");
        CUSTOM_SERIAL.println(demarcateTimes);
        if (demarcateTimes == 0) { demarcating = false; break; }
    }

    // ---- 串口接收：字节帧 + // 明文 ----
    while (CUSTOM_SERIAL.available()) {
        byte inByte = (byte)CUSTOM_SERIAL.read();

        // 起始标志 / 明文检测
        if (!isRecording && inByte == 0xAA) {
            isRecording = true;
        } else if (!isRecording) {
            if (inByte == 0x2f) {   // '/'
                if (!plainTextMark) { plainTextMark = true; }
                else {
                    commandParse(CUSTOM_SERIAL.readString());  // //xxx 明文命令
                    plainTextMark = false;
                    break;
                }
            }
        }

        // 记录模式
        if (isRecording) {
            if (inByte == 0xDD) {
                commandParse(ByteArrayToCommand(receivedData, indexInSerial));
                isRecording = false;
                indexInSerial = 0;
                break;
            } else if (inByte == 0xAA) {
                isRecording = true;
                indexInSerial = 0;
            } else {
                receivedData[indexInSerial++] = inByte;
            }
            if (indexInSerial >= (int)sizeof(receivedData)) {
                indexInSerial = 0;
                isRecording = false;
            }
        }
    }
}
