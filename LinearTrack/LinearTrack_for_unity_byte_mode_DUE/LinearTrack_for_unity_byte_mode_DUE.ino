#include <DueTimer.h>
#include <RingBuf.h>
#include <algorithm>

// =====================================================================
// LinearTrack 单块 Arduino Due 固件
// 由两块 AVR Arduino 合并而来：
//   - VR_lick_for_unity_byte_mode（运动编码器 + 舔水检测 + 奖励给水 + ws 上报）
//   - samll_arduino_new_Lroom_A（手动给水按钮）
// 结构仿照 LEDRing_for_unity_byte_mode_NewVersion_V2_5（握手 / DueTimer /
// 字节帧协议 / //forceinit 明文 / DoubleBuffer 双缓冲 / 标量与数组双变量命名空间）。
// 协议索引与变量表顺序与 Unity 端 LinearTrackMoving.cs 的 lsTypes /
// Arduino_var_list / Arduino_ArrayTypeVar_list 完全一致，勿改顺序。
// demarcate 标定功能与舔水指示 LED 已删除；lick 与 pump 为 4 通道，
// trial / 奖励区上下文等仍为全局标量。
// =====================================================================

// ========== 通讯方式配置 ==========
// 设置为 1 使用 USB 通讯 (SerialUSB, Native 口)，设置为 0 使用串口通讯 (Serial, Programming 口)
#define USE_USB_SERIAL 1
#if USE_USB_SERIAL
  #define CUSTOM_SERIAL SerialUSB
#else
  #define CUSTOM_SERIAL Serial
#endif

String VERSION = "LT2.0";  // 版本号，Unity config.ini serialSettings/compatibleVersion 需匹配（或留空）

#define CHANNEL_COUNT 4    // lick / pump 通道数（数组变量长度）

// ========== 引脚表（Due 重新设计，用户按此接线）==========
// 运动编码器（跑球/转轮，两通道正交）—— 高频信号，留在真中断脚
#define ENCODER_INT_PIN    2     // 中断触发（原 VR_lick pin2）
#define ENCODER_A_PIN      3     // 通道A（原 pin23）
#define ENCODER_B_PIN      4     // 通道B（原 pin25）
// 手动给水按钮：仅轮询、不需要中断 -> 分配到板子下排（22 起那一列）
#define MANUAL_WATER_PIN   30    // 手动给水按钮（按下对 pump 通道 0 持续给水）
#define TEST_PIN           13    // 板载LED / 示波器同步

// lick 输入 22,24,26,28 / pump 输出 23,25,27,29（用户规则：lick 偶数、pump 奇数）
// 注意：Due 的 22-53 全部为 NOT_ON_PWM/NOT_ON_TIMER，不能硬件 PWM；
// 本固件用 DueTimer(TC 通道，与引脚无绑定) + digitalWrite 产生脉宽，不受影响。
int lickReadPins[CHANNEL_COUNT]  = {22, 24, 26, 28};
int waterServePins[CHANNEL_COUNT] = {23, 25, 27, 29};

// 水阀极性：改为高电平有效，对齐 V2_5
#define WATER_ACTIVE       HIGH
#define WATER_IDLE         LOW

// ========== 舔水电平定义 ==========
// 内部上拉 + 空闲 HIGH + 舔水拉低（Due 无内部下拉，core 中 INPUT_PULLDOWN/PPDR 零命中，
// 故 "HIGH 有效" 在本平台上物理不可实现；V2_5 的 LICK_ACTIVE=HIGH 是死代码，其 setup() 实际
// 用的就是 pinMode(INPUT)+digitalWrite(HIGH) 上拉方案，与此处一致）。
// 该配置下断线/悬空 -> 读到 HIGH -> 判为静默，失效方向安全（不会误报舔水）。
#define LICK_REVERSE false  // 若 touch panel 实际是导通拉高，把这里改 true 即可
#if LICK_REVERSE
  const int LICK_ACTIVE  = 1;
  const int LICK_SILENCE = 0;
#else
  const int LICK_ACTIVE  = 0;
  const int LICK_SILENCE = 1;
#endif
#define LICK_MIN_MILLIS 20  // 一次完成的舔的最短导通时长(ms)，滤除天线效应毛刺

// ========== 标量实验变量（顺序 = Unity Arduino_var_list，下标 0-8）==========
int enter_reward_context = 0;    int* p_enter_reward_context = &enter_reward_context;
int in_reward_context = 0;       int* p_in_reward_context = &in_reward_context;
int lick_mode = 0;               int* p_lick_mode = &lick_mode;      // 仅作数值同步，不驱动出水
int trial = 0;                   int* p_trial = &trial;
int lick_count_max = 0;          int* p_lick_count_max = &lick_count_max;
int lick_mode0_delay = 0;        int* p_lick_mode0_delay = &lick_mode0_delay;
int lick_mode1_delay = 0;        int* p_lick_mode1_delay = &lick_mode1_delay; // 普通延时变量
int serveWaterReward = -1;       int* p_serveWaterReward = &serveWaterReward; // -1 空闲；0..3 = 出水通道（对齐 V2_5 waterServeManual 语义）
int INDEBUGMODE = 0;             int* p_INDEBUGMODE = &INDEBUGMODE;

//                        0                      1                 2           3         4                  5                    6                    7                   8
int* pointer_array[] = {p_enter_reward_context, p_in_reward_context, p_lick_mode, p_trial, p_lick_count_max, p_lick_mode0_delay, p_lick_mode1_delay, p_serveWaterReward, p_INDEBUGMODE};
const int POINTER_ARRAY_COUNT = sizeof(pointer_array) / sizeof(pointer_array[0]);

// ========== 数组实验变量（顺序 = Unity Arduino_ArrayTypeVar_list；命令形如 <arrIdx>[<elemIdx>]=<val>）==========
// 与标量表是两个独立命名空间，靠命令里有没有 '[' 区分（与 V2_5 一致，数组下标 0/1/2 对应顺序照抄 V2_5）
int waterServeMicros[CHANNEL_COUNT] = {5000, 5000, 5000, 5000};  int* p_waterServeMicros = waterServeMicros;
int lick_count[CHANNEL_COUNT] = {};                             int* p_lick_count = lick_count;
int water_flush[CHANNEL_COUNT] = {};                            int* p_water_flush = water_flush;
int* pointerArrayType_array[] = {p_waterServeMicros, p_lick_count, p_water_flush};
int  pointerArrayType_arrayLength[] = {CHANNEL_COUNT, CHANNEL_COUNT, CHANNEL_COUNT};
const int ARRAY_TYPE_COUNT = sizeof(pointerArrayType_array) / sizeof(pointerArrayType_array[0]);

//                          0        1               2       3        4               5
// 顺序 = Unity lsTypes 位置：move / context_info / log / echo / value_change / command（仅数组下标参与传输）
String serial_print_type[] = {"move", "context_info", "log", "echo", "value_change", "command"};

// ========== 给水参数 ==========
int manualPulseMillis = 100;    // 手动持续给水单次脉宽(ms)

// ========== 采样 / 计时 ==========
const long interval_samplerate = 10;   // 10ms 节拍，仅用于递减 lick_mode*_delay
unsigned long previousMillis3 = 0;      // 节拍计时

// ========== lick 中断状态（内部，不进任何对外变量表）==========
volatile unsigned long lickStartMillis[CHANNEL_COUNT];
volatile bool lickActive[CHANNEL_COUNT];

// ========== 运动编码器 ==========
volatile long encoderLength = 0;

// ========== 串口接收 ==========
byte receivedData[256];
int  indexInSerial = 0;
bool isRecording = false;
bool plainTextMark = false;
// 明文命令行累积（取代 readString()，见 loop 中说明）
char plainTextBuf[128];
int  plainTextLen = 0;
bool plainTextActive = false;
const int RX_MAX_BYTES_PER_LOOP = 64;   // 单次 loop 最多处理的接收字节数，防止 Unity 灌包饿死主循环

// ========== 握手 ==========
bool handshakeDone = false;

// ========== 发送节流 ==========
int maxMsgCountPerChunk = 20;
#define BUFFER_MSG_MAX 100   // 双缓冲单条消息长度上限(含结尾0)，与 bufferGetString 的校验一致

// ========== 定时器 ==========
// 每通道一个 DueTimer：4 路可同时出水、各自独立脉宽（Timer0..3 = TC0ch0/1/2 + TC1ch0，NUM_TIMERS=9）。
// 直接引用库自带的全局对象，不用 Timer.getAvailable()（它只返回第一个 callbacks[i]==NULL 的槽位、
// 并不预留，attachInterrupt 之前两次调用会拿到同一个定时器）。
DueTimer* pumpTimers[CHANNEL_COUNT] = { &Timer0, &Timer1, &Timer2, &Timer3 };
volatile bool pumpBusy[CHANNEL_COUNT] = { false, false, false, false };

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
    byte temp_buffer[280];   // stringToByteArray 最大写到下标 3+255=258，256 会越界 3 字节
    int temp_length = stringToByteArray(inputStr, temp_buffer);
    serial_sendRaw(temp_buffer, temp_length);
}

bool bufferPushChar(const char* msg) {   // ISR/主循环均可入队
    return sendDataBuffers.push(msg);
}
bool bufferPushString(String strmsg) {
    return bufferPushChar(strmsg.c_str());
}

// 取出一条消息填入调用方缓冲，返回消息长度(不含结尾0)；0 表示本次没取到。零堆分配。
// 返回 0 的三种情形调用方都应停止本轮排空（尤其"不完整"必须 break，否则主循环空转）：
//   空 / 消息尚未被 ISR 推完 / 数据损坏(已丢 1 字节重同步)
int bufferGetString(RingBuf<char, 1024>* buffer, char* out, int outMax) {
    if (buffer->isEmpty()) { return 0; }

    char _len;
    buffer->peek(_len, 0);                 // 非空时 peek(x,0) 必然成功，避免 pop 后再 push 回队尾
    int len = (unsigned char)_len;         // 长度字节按无符号解释：push 写入的是 len+1，>127 时 char 会变负

    if (len == 0 || len > BUFFER_MSG_MAX) {   // 损坏：只丢 1 字节重同步，不 clear() 整包(会连带丢掉后面所有完好消息)
        char _t; buffer->pop(_t);
        return 0;
    }
    if ((int)buffer->size() < len) { return 0; }   // 消息未推完，等下一轮

    char _end;
    buffer->peek(_end, len - 1);           // size()>=len>len-1，绕开 RingBuf::peek 的 off-by-one
    if (_end != 0) {                       // 损坏：只丢 1 字节重同步
        char _t; buffer->pop(_t);
        return 0;
    }
    if (len - 1 > outMax - 1) {            // 调用方缓冲太小，丢弃整条
        for (int i = 0; i < len; i++) { char _t; buffer->pop(_t); }
        return 0;
    }
    for (int i = 0; i < len; i++) { buffer->pop(out[i]); }
    out[len - 1] = '\0';
    return len - 1;
}

void SerialLog(String inputStr, String input_head = "") {
    bufferPushString("log:" + input_head + inputStr);
}

// =====================================================================
// 给水（每通道一个 DueTimer，4 路可并发、脉宽各自独立）
// =====================================================================
bool anyPumpBusy() {
    for (int i = 0; i < CHANNEL_COUNT; i++) { if (pumpBusy[i]) { return true; } }
    return false;
}

void pump_off(int ch) {
    digitalWrite(waterServePins[ch], WATER_IDLE);
    pumpBusy[ch] = false;
    pumpTimers[ch]->stop();
    if (!anyPumpBusy()) { digitalWrite(TEST_PIN, LOW); }   // 所有通道都空闲才拉低同步脚，避免重叠脉冲被提前截断
}
// DueTimer 回调必须是无参函数，故每通道一个包装
void PumpOffISR0() { pump_off(0); }
void PumpOffISR1() { pump_off(1); }
void PumpOffISR2() { pump_off(2); }
void PumpOffISR3() { pump_off(3); }
void (*const PumpOffISR[CHANNEL_COUNT])() = { PumpOffISR0, PumpOffISR1, PumpOffISR2, PumpOffISR3 };

void pump_set(int ch, int micros, bool write_mode = true) { // write_mode: true 同通道冲突则不设，false 冲突则覆盖
    if (ch < 0 || ch >= CHANNEL_COUNT) { return; }
    if (pumpBusy[ch] && write_mode) { return; }   // 仅同通道互斥（原来的跨通道互斥在 4 路并发下会互相阻塞）
    // flush 不重要：定时出水直接结束该通道的 flush，数组跟着引脚走
    if (water_flush[ch]) { water_flush[ch] = 0; }
    // 先占忙位再拉脚：SAM3X 每个 TC 通道是独立 IRQ，pump_off(x) 可能在这里插进来，
    // 若此时本通道忙位尚未置上，anyPumpBusy() 会误判为全空闲并把 TEST_PIN 提前拉低。
    pumpBusy[ch] = true;
    digitalWrite(waterServePins[ch], WATER_ACTIVE);
    digitalWrite(TEST_PIN, HIGH);
    char ws[24];
    snprintf(ws, sizeof(ws), "context_info:ws:%d", ch);   // 带通道号；Unity 端 StartsWith("ws") 仍兼容
    bufferPushChar(ws);
    pumpTimers[ch]->attachInterrupt(PumpOffISR[ch]).start(micros);
}

// 直接阻塞脉冲（手动按钮用，不上报 ws，避免刷屏）：仅作用于 pump 通道 0
void pulseWaterDirect(int ms) {
    if (anyPumpBusy()) { return; }             // 实验给水进行中则跳过
    if (water_flush[0]) { water_flush[0] = 0; }  // 同样维持"数组跟着引脚走"
    digitalWrite(waterServePins[0], WATER_ACTIVE);
    digitalWrite(TEST_PIN, HIGH);
    delay(ms);
    digitalWrite(waterServePins[0], WATER_IDLE);
    digitalWrite(TEST_PIN, LOW);
}

// =====================================================================
// 运动编码器 ISR
// =====================================================================
void CalcLengthCallByInterrupt() {
    encoderLength += digitalRead(ENCODER_A_PIN) - digitalRead(ENCODER_B_PIN);
}

// =====================================================================
// 舔水 ISR（内部上拉：空闲 HIGH，舔水拉低）
// Due 的每个 (PIO 控制器, 位) 都有独立回调槽（core 的 WInterrupts.c: callbacksPioA..D[32]），
// 不存在 STM32 那种"EXTI 线号相同即互斥"的稀缺问题，4 路 lick 同时挂中断没有冲突；
// 同一 PIO 内的多个脚只是在 PIOx_Handler 里按位串行分发。
// 必须挂 CHANGE 而不是 RISING/FALLING 各一次：同一引脚的第二次 attachInterrupt 会覆盖
// 该 (port,bit) 的回调槽，故在 ISR 内读电平区分起止沿。
// 取代原来的 10ms 轮询：轮询既有采样漂移，又对同一引脚连读两次可能读到不同电平。
// =====================================================================
void lick_edge(int ch) {
    bool active = (digitalRead(lickReadPins[ch]) == LICK_ACTIVE);
    if (active) {                                   // 起始沿
        lickStartMillis[ch] = millis();
        lickActive[ch] = true;
    } else if (lickActive[ch]) {                    // 结束沿：够长才算一次完成的舔
        lickActive[ch] = false;
        if (millis() - lickStartMillis[ch] >= LICK_MIN_MILLIS) {
            lick_count[ch]++;
            char entry[28];
            snprintf(entry, sizeof(entry), "context_info:lick:%d", ch);
            bufferPushChar(entry);                  // 只上报，不判定正确性、不自动出水
        }
    }
}
void LickISR0() { lick_edge(0); }
void LickISR1() { lick_edge(1); }
void LickISR2() { lick_edge(2); }
void LickISR3() { lick_edge(3); }
void (*const LickISR[CHANNEL_COUNT])() = { LickISR0, LickISR1, LickISR2, LickISR3 };

// =====================================================================
// 状态 / 初始化 / 命令
// =====================================================================
void print_status(String _head = "") {
    // 本消息约 180 字符，超过 BUFFER_MSG_MAX(100)，走双缓冲必被丢弃 -> 直接 serial_send
    // 用 static 缓冲 + snprintf，消掉原来的 new char[256]/delete[]（print_status 仅 check 命令触发，非热路径）
    static char body[280];
    int n = snprintf(body, sizeof(body),
            " ez %d iz %d lm %d tr %d lcm %d d0 %d d1 %d ch %d dbg %d | us %d,%d,%d,%d | lk %d,%d,%d,%d | fl %d,%d,%d,%d",
            enter_reward_context, in_reward_context, lick_mode, trial, lick_count_max,
            lick_mode0_delay, lick_mode1_delay, serveWaterReward, INDEBUGMODE,
            waterServeMicros[0], waterServeMicros[1], waterServeMicros[2], waterServeMicros[3],
            lick_count[0], lick_count[1], lick_count[2], lick_count[3],
            water_flush[0], water_flush[1], water_flush[2], water_flush[3]);
    if (n < 0 || n >= (int)sizeof(body)) { serial_send("log:status too long, skipped"); return; }
    serial_send("log:" + _head + body);
}

void init_by_PC(bool resetConnectStatus = true) {
    // === 标量实验变量清零 ===
    // INDEBUGMODE 按项目既有约定保留（软复位后仍能看到调试输出），不在此清零
    enter_reward_context = 0;
    in_reward_context = 0;
    lick_mode = 0;
    lick_count_max = 0;
    lick_mode0_delay = 0;
    lick_mode1_delay = 0;
    serveWaterReward = -1;   // -1 = 空闲，不再是"置 1 出水"
    trial = 0;

    // === 数组实验变量清零 ===
    // waterServeMicros 是配置而非运行时状态，对齐 V2_5 不在 reset 里覆盖
    std::fill(std::begin(lick_count), std::end(lick_count), 0);
    std::fill(std::begin(water_flush), std::end(water_flush), 0);

    // === 计时复位 ===
    previousMillis3 = 0;

    // === 停止全部通道给水 / 输出复位 ===
    for (int i = 0; i < CHANNEL_COUNT; i++) {
        pumpTimers[i]->stop();
        pumpBusy[i] = false;
        lickActive[i] = false;
        lickStartMillis[i] = 0;
        digitalWrite(waterServePins[i], WATER_IDLE);
    }
    digitalWrite(TEST_PIN, LOW);

    // === 通信状态复位 ===
    isRecording = false;
    indexInSerial = 0;
    plainTextMark = false;
    plainTextActive = false;
    plainTextLen = 0;
    encoderLength = 0;

    // === 发送缓冲清空 ===
    // swap() 会清空"交换后的写缓冲"（即交换前的读缓冲）。forceinit 要丢弃全部待发报告，
    // 所以连续交换两次：第一次清掉旧读缓冲，第二次清掉那份尚未发送的旧写缓冲，同时恢复原来的读/写指派。
    // 不再依赖 shouldSwap() 标志：该标志只在写缓冲过 80% 时置位，未置位时旧代码会一份都不清。
    sendDataBuffers.swap();
    sendDataBuffers.swap();

    if (resetConnectStatus) { handshakeDone = false; }
    SerialLog("initialed");
}

// echo 两条出口都必须保留：明文 println 供 Unity Context_verify 的 ReadLine 同步校验；
// 字节帧供 Unity 异步 commandVerifyDict 清除（V2_5 能只发明文是因为它走 inVerifyOrVerifyNeedless:true
// 根本不填字典，本工程会填，去掉字节帧会导致命令每帧重发直到 2s 超时）。
void echo_send(const String& _command) {
    char echoContent[280];
    // 命令内容上限 250 字节，"echo:" + 250 + ":echo" = 261 > 256，sprintf 会栈溢出 -> snprintf 截断
    snprintf(echoContent, sizeof(echoContent), "echo:%s:echo", _command.c_str());
    CUSTOM_SERIAL.println(echoContent);
    bufferPushChar(echoContent);
}

void commandParse(String _command) {
    _command.replace("\n", "");
    _command.replace("\r", "");

    if (_command.compareTo("check") == 0)      { print_status(); return; }
    else if (_command.compareTo("forceinit") == 0) { init_by_PC(true);  return; }
    else if (_command.compareTo("clear") == 0)     { init_by_PC(false); return; }
    else if (_command.compareTo("ping") == 0)      { return; }  // 保活，忽略

    int equal_pos = _command.indexOf('=');
    if (equal_pos <= 0) { return; }

    int arrayStartIndc = _command.indexOf('[');
    int arrayEndIndc   = _command.indexOf(']');

    // ===== 数组变量：<arrIdx>[<elemIdx>]=<val>（与标量表是两个独立命名空间，靠有没有 '[' 区分，同 V2_5）=====
    if (arrayStartIndc > 0) {
        if (arrayEndIndc <= arrayStartIndc) { return; }   // 有 '[' 没 ']'：坏消息，忽略
        String arrStr = _command.substring(0, arrayStartIndc);
        int input_ArrVar = arrStr.toInt();
        if (input_ArrVar == 0 && arrStr.compareTo("0") != 0) { return; }
        String elemStr = _command.substring(arrayStartIndc + 1, arrayEndIndc);
        int input_ArrInd = elemStr.toInt();
        if (input_ArrInd == 0 && elemStr.compareTo("0") != 0) { return; }
        // V2_5 直接用 input_ArrVar 索引 arrayLength，缺这一层边界检查会 OOB 读
        if (input_ArrVar < 0 || input_ArrVar >= ARRAY_TYPE_COUNT) { return; }
        if (input_ArrInd < 0 || input_ArrInd >= pointerArrayType_arrayLength[input_ArrVar]) { return; }

        int input_value = (_command.substring(equal_pos + 1)).toInt();

        if (pointerArrayType_array[input_ArrVar] == p_water_flush) {
            // flush 锁存语义（手动控制，程序不会自动 flush）：
            //   值 0            -> 显式关闭
            //   值非 0 且未 flush -> 第一次收到：该通道持续不断给水，不启动任何定时器
            //   值非 0 且已 flush -> 第二次收到：切换关闭
            if (input_value == 0) {
                water_flush[input_ArrInd] = 0;
            } else {
                water_flush[input_ArrInd] = (water_flush[input_ArrInd] > 0) ? 0 : 1;
            }
            if (water_flush[input_ArrInd] > 0) {
                // flush 优先：该通道的定时脉冲让位给持续给水（停表、置空闲标志，引脚由下面直接驱动）
                if (pumpBusy[input_ArrInd]) {
                    pumpTimers[input_ArrInd]->stop();
                    pumpBusy[input_ArrInd] = false;
                }
                digitalWrite(waterServePins[input_ArrInd], WATER_ACTIVE);
                digitalWrite(TEST_PIN, HIGH);
            } else {
                digitalWrite(waterServePins[input_ArrInd], WATER_IDLE);
                if (!anyPumpBusy()) { digitalWrite(TEST_PIN, LOW); }
            }
        } else {
            *(pointerArrayType_array[input_ArrVar] + input_ArrInd) = input_value;
        }
        echo_send(_command);
        return;
    }

    // ===== 标量变量：<idx>=<val> =====
    String varStr = _command.substring(0, equal_pos);
    int input_var = varStr.toInt();
    // String::toInt() 对非数字前缀返回 0，会静默改写 pointer_array[0]（= enter_reward_context）。
    // 可复现触发：Unity LinearTrackUIUpdate 手动串口框输入 "p_lick_mode=1"（不带 / 前缀）
    // -> DataSend 走 lsTypes[5] 的 cmd: 帧 -> 此处 toInt() 得 0。仅当字面量确实是 "0" 时放行。
    if (input_var == 0 && varStr.compareTo("0") != 0) { return; }
    int input_value = (_command.substring(equal_pos + 1)).toInt();
    if (input_var >= 0 && input_var < POINTER_ARRAY_COUNT) {
        *(pointer_array[input_var]) = input_value;
        echo_send(_command);
    }
}

// =====================================================================
// 失效保护：看门狗 + HardFault（两个 core 符号都是 weak，可从 sketch 覆盖）
// watchdogSetup 在 main() 里、init()/initVariant()/setup() 之前被调用。core 的 weak 默认实现
// 是 WDT_Disable()；这里保留同样的显式禁用，而不是覆盖成空函数——否则若 WDT 在上一次运行中
// 已被启用（看门狗复位后 MR 可能仍保持使能），从复位瞬间到 setup() 末尾这段 ROM bootloader +
// CDC 枚举 + 阻塞式握手的时间内狗一直在跑，会造成开机复位风暴。
// 真正启用改到 setup() 最后一句，握手完成之后。
// =====================================================================
void watchdogSetup(void) { watchdogDisable(); }

// HardFault_Handler 由 cores/arduino/cortex_handlers.c(C 编译器) 定义、由 exceptions.c 引用，是不加修饰的 C 符号，
// 所以必须用 extern "C" 定义才能覆盖那个 weak alias 到 __halt 的实现。
extern "C" void HardFault_Handler(void) {
    watchdogEnable(1000);      // 改写 MR 把超时压到 1s（setup() 里是 4s）
    while (1) { }              // 停在原地交给看门狗复位，避免带着损坏状态继续执行
}

// =====================================================================
// 握手（对齐 V2.4 与 Unity CreateSerialConnection）
// =====================================================================
void doHandshake() {
    while (!handshakeDone) {
        watchdogReset();       // 握手是阻塞式的；未连接时可能长期停留在此，必须喂狗否则会被反复复位
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
    // === 水阀引脚最先初始化 ===
    // 目的：尽量缩短上电后泵脚处于高阻、阀状态不确定的窗口。
    // 无法完全消除（setup 之前还有 ROM bootloader / CDC 初始化阶段），彻底关闭需要外部上下拉电阻，
    // 用户已选择"只做软件，暂不动硬件"。
    for (int i = 0; i < CHANNEL_COUNT; i++) {
        pinMode(waterServePins[i], OUTPUT);
        digitalWrite(waterServePins[i], WATER_IDLE);
    }
    pinMode(TEST_PIN, OUTPUT);
    digitalWrite(TEST_PIN, LOW);

    // === 舔水输入：内部上拉（空闲 HIGH，舔水拉低；Due 无内部下拉，断线/悬空会读到 HIGH -> 判静默，失效方向安全）===
    for (int i = 0; i < CHANNEL_COUNT; i++) {
        pinMode(lickReadPins[i], INPUT);
        digitalWrite(lickReadPins[i], HIGH);
    }

    // 运动编码器
    pinMode(ENCODER_INT_PIN, INPUT);
    pinMode(ENCODER_A_PIN, INPUT);
    pinMode(ENCODER_B_PIN, INPUT);
    digitalWrite(ENCODER_A_PIN, HIGH);
    digitalWrite(ENCODER_B_PIN, HIGH);

    // 手动给水按钮（仅轮询，不需要中断）
    pinMode(MANUAL_WATER_PIN, INPUT);
    digitalWrite(MANUAL_WATER_PIN, HIGH);

    // PIO 中断优先级降到 1（core 默认 0），使舔水/编码器中断不抢占 TC 定时器中断，保护给水脉宽精度。
    // 对齐 V2_5；Due 的 attachInterrupt 走 PIOA..PIOD 四个控制器，需逐个设置。
    NVIC_SetPriority(PIOA_IRQn, 1);
    NVIC_SetPriority(PIOB_IRQn, 1);
    NVIC_SetPriority(PIOC_IRQn, 1);
    NVIC_SetPriority(PIOD_IRQn, 1);

    attachInterrupt(digitalPinToInterrupt(ENCODER_INT_PIN), CalcLengthCallByInterrupt, RISING);
    for (int i = 0; i < CHANNEL_COUNT; i++) {
        // CHANGE 而非 RISING+FALLING 各一次：同一 (port,bit) 只有一个回调槽，第二次 attach 会覆盖第一次
        attachInterrupt(digitalPinToInterrupt(lickReadPins[i]), LickISR[i], CHANGE);
    }

#if USE_USB_SERIAL
    CUSTOM_SERIAL.begin(115200);   // SerialUSB 忽略波特率，保持与 Unity config.ini 一致即可
#else
    CUSTOM_SERIAL.begin(250000);   // Programming 口走真 UART，用高速率（Unity serialSpeed 白名单含 250000）
#endif

    // 握手：不断发送 initialed:VERSION 直到收到 ACK
    handshakeDone = false;
    doHandshake();

    // 看门狗放在握手之后启用：握手是阻塞式的，提前启用会在 Unity 未开机时反复复位板子
    watchdogEnable(4000);
}

// =====================================================================
// loop
// =====================================================================
void loop() {
    watchdogReset();   // 喂狗：本函数或 doHandshake 卡死超过 4s 时整板复位（Unity 侧表现为一次断线重连）

    // 握手保持（掉线/forceinit 后重新握手）
    if (!handshakeDone) { doHandshake(); }

    // ---- 排空发送双缓冲 ----
    // 不再无条件 shouldSwap()->swap()：swap() 会 clear() 新写缓冲(即旧读缓冲)，
    // 旧读缓冲尚未排空时交换会丢掉所有未发送消息（原实现在写缓冲过 80% 时就会这样丢）。
    // 改为只在读缓冲确实排空后才交换。
    RingBuf<char, 1024>* readBuffer = sendDataBuffers.getReadBuffer();
    int _count = 0;
    while (_count < maxMsgCountPerChunk && CUSTOM_SERIAL.availableForWrite() > 5) {
        char msg[BUFFER_MSG_MAX + 1];
        int n = bufferGetString(readBuffer, msg, sizeof(msg));
        if (n == 0) { break; }   // 空/未推完/已重同步；必须 break，否则原实现会在此死循环
        serial_send(String(msg));
        _count++;
    }
    if (readBuffer->isEmpty() && !sendDataBuffers.getWriteBuffer()->isEmpty()) {
        sendDataBuffers.swap();
    }

    // ---- 运动上报（二进制帧，直接发送，绕开字符串缓冲）----
    // 关中断快照：encoderLength 是 volatile，原来的 !=0 / abs() / +64 / =0 是四次独立访问，
    // ISR 可在其间插入 -> 计数撕裂；且 |delta|>=16 时原实现跳过发送却仍清零 -> 整段运动无声丢失。
    // 改为无损多帧排空，单帧幅度 ±63（chunk+64 ∈ [1,127]，不会撞上 0xAA/0xDD；
    // Unity 侧 CommandParse case 0 只做 value-64，无需改动）。
    noInterrupts();
    long delta = encoderLength;
    encoderLength = 0;
    interrupts();
    while (delta != 0) {
        int chunk = (int)constrain(delta, -63, 63);
        byte move_buffer[32];
        int temp_length = stringToByteArray("move:xy", move_buffer);
        move_buffer[temp_length - 3] = (byte)(chunk + 64);   // dx
        move_buffer[temp_length - 2] = (byte)64;             // dy = 0 -> +64
        serial_sendRaw(move_buffer, temp_length);
        delta -= chunk;
    }

    // ---- 进出奖励区（上下文状态仍是全局标量）----
    // lick_count 现在是"每通道已完成舔水次数"，不再随进出区自动清零；
    // 需要复位时由 Unity 显式发 1[ch]=0。原来的 lick_count 奖励额度语义连同自动出水逻辑一起废除。
    if (enter_reward_context > 0) {
        in_reward_context = 1;
        enter_reward_context = 0;
    } else if (enter_reward_context == -1) {
        enter_reward_context = 0;
        in_reward_context = 0;
    }

    // ---- 10ms 节拍：只用于递减 lick_mode*_delay ----
    // 原来这里还做舔电平轮询和指示 LED：轮询已由 lick 的 CHANGE 中断取代，LED 已删除。
    // 基准用 += 递进而非赋值为 now，消除每次节拍的累积漂移；但长时间停顿（手动脉冲 delay(100)、
    // 握手等待）后重新对齐，避免一次补偿性连跳把 delay 直接烧光。
    unsigned long now3 = millis();
    if (now3 - previousMillis3 >= (unsigned long)interval_samplerate) {
        previousMillis3 = (now3 - previousMillis3 > 4 * (unsigned long)interval_samplerate)
                        ? now3 : previousMillis3 + interval_samplerate;
        if (lick_mode1_delay > 0) { lick_mode1_delay--; }
        if (lick_mode0_delay > 0) { lick_mode0_delay--; }
    }

    // ---- 舔水上报已移到 lick_edge()（CHANGE 中断），此处不再轮询 ----
    // 原 lick_mode==0/1 的自动出水与 correct 判定逻辑已删除（判定移至 Unity LickingCheck）：
    //   mode0: in_reward_context 内 lick_count 递减达标则 pump_set + "lick:correct"
    //   mode1: lick_mode1_delay==0 时上报 correct；lick_mode1_delay==-2 时 pump_set
    // 保留注释以便追溯。lick_mode/lick_mode0_delay/lick_mode1_delay 仅作数值同步，不再驱动出水。

    // ---- Unity 奖励出水命令：serveWaterReward 携带通道号（-1 空闲，0..3），与 lick_mode 无关 ----
    if (serveWaterReward >= 0) {
        int ch = serveWaterReward;
        serveWaterReward = -1;
        pump_set(ch, waterServeMicros[ch]);
    }

    // ---- flush 不在此处：flush 是 commandParse 里的锁存直驱，程序不会自动 flush ----

    // ---- 手动给水按钮（整合自 small arduino）：按下对通道 0 持续给水 ----
    // 只在所有 pump 都空闲时执行：pulseWaterDirect 内部是阻塞 delay()，
    // 与异步 pump 定时器同时动作会让 TEST_PIN 的拉低时刻错乱。
    if (digitalRead(MANUAL_WATER_PIN) == LOW && !anyPumpBusy()) {
        pulseWaterDirect(manualPulseMillis);
        delay(5);
    }

    // ---- 串口接收：字节帧 + // 明文 ----
    // 明文命令改为逐字节累积到行缓冲，不再用 readString()：readString() 会阻塞到静默或 1000ms 超时，
    // 这期间主循环不排空双缓冲（ISR 仍在 push -> 溢出丢舔水报文）、不处理 serveWaterReward / water_flush、
    // 不响应手动按钮。（DueTimer 与编码器 ISR 本身不受影响。）
    // 单次 loop 限量 RX_MAX_BYTES_PER_LOOP 字节，防止 Unity 灌包饿死主循环其余部分。
    int rxCount = 0;
    while (CUSTOM_SERIAL.available() && rxCount < RX_MAX_BYTES_PER_LOOP) {
        rxCount++;
        byte inByte = (byte)CUSTOM_SERIAL.read();

        if (inByte == 0xAA) {              // 帧起始优先级最高，打断明文累积 / 帧内重同步
            plainTextMark = false;
            plainTextActive = false;
            plainTextLen = 0;
            isRecording = true;
            indexInSerial = 0;
            continue;
        }

        if (isRecording) {
            if (inByte == 0xDD) {          // 帧结束
                commandParse(ByteArrayToCommand(receivedData, indexInSerial));
                isRecording = false;
                indexInSerial = 0;
            } else {
                receivedData[indexInSerial++] = inByte;
                if (indexInSerial >= (int)sizeof(receivedData)) {   // 无结束符的坏帧，丢弃重同步
                    indexInSerial = 0;
                    isRecording = false;
                }
            }
            continue;
        }

        if (inByte == 0x2f) {              // '/'
            if (!plainTextMark) { plainTextMark = true; }
            else { plainTextActive = true; plainTextLen = 0; }   // 第二个 '/'，开始累积命令行
        } else if (plainTextActive) {
            if (inByte == '\n' || inByte == '\r') {
                plainTextBuf[plainTextLen] = '\0';
                plainTextActive = false;
                plainTextMark = false;
                plainTextLen = 0;
                if (plainTextBuf[0] != '\0') { commandParse(plainTextBuf); }
            } else if (plainTextLen < (int)sizeof(plainTextBuf) - 1) {
                plainTextBuf[plainTextLen++] = (char)inByte;
            } else {                       // 超长明文行，整行丢弃
                plainTextActive = false;
                plainTextMark = false;
                plainTextLen = 0;
            }
        }
    }
}
