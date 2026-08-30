# config.py — 集中配置：引脚映射 + 协议常量契约
# 所有引脚/NUM 改动都集中在这里，改一行即可生效。

# ============ 版本（用于握手 initialed:<VERSION>） ============
VERSION = "V2.4"

# ============ 通讯方式 ============
# True  -> USB CDC (pyb.USB_VCP)，对应 Arduino 的 USE_USB_SERIAL=1 (SerialUSB)
# False -> 物理 UART (pyb.UART)，对应 Arduino 的 Serial
USE_USB_SERIAL = True

if USE_USB_SERIAL:
    VCP_MODE = "CDC"        # boot.py 里 pyb.usb_mode 的参数
else:
    VCP_MODE = "MSC"        # 用 UART 时仍保留 MSC，方便拷贝文件
    UART_ID = 2             # 用 UART2 (X3/X4)
    UART_BAUD = 250000      # 对应原代码 Serial 的 250000

# ============ 引脚映射（pyboard 引脚名，同类分侧聚集、连续排列） ============
# 设计原则：
#   - 输出（给水泵/OG/miniscope/spare）集中在右侧排针，给水泵 8 路在 Y5~Y12 完全连续。
#   - 输入（舔水/传感器）集中在左侧排针 X1~X8（X5 被光功率 DAC 占用，故舔水 7 落在 Y3）。
#   - 给水/OG 用「软件单脉冲」（共享定时器回调），对引脚无定时器复用约束，故可连续排列。

# 8 路舔水检测（输入，ExtInt）
#   lick0 的 REACH 绑 LICK_PINS[0] RISING；lick0 的 LEAVE 绑 LICK_PINS[1] FALLING（对标原代码）
LICK_PINS = [
    "X1",  # lick 0, PA0, RISING(REACH)
    "X2",  # lick 1, PA1, FALLING(leave)
    "X3",  # lick 2, PA2 (预留)
    "X4",  # lick 3, PA3 (预留)
    "X6",  # lick 4, PA5 (预留)
    "X7",  # lick 5, PA6 (预留)
    "X8",  # lick 6, PA7 (预留)
    "Y3",  # lick 7, PB8 (预留)
]

# 8 路给水泵（输出，软件单脉冲，共享定时器回调）—— Y5~Y12 连续 8 引脚
PUMP_PINS = [
    "Y5",   # pump 0, PB12
    "Y6",   # pump 1, PB13
    "Y7",   # pump 2, PB14
    "Y8",   # pump 3, PB15
    "Y9",   # pump 4, PB10
    "Y10",  # pump 5, PB11
    "Y11",  # pump 6, PB0
    "Y12",  # pump 7, PB1
]

# 给水泵默认时长（微秒），对应原 waterServeMicros
WATER_SERVE_MICROS = [10000] * 8

# OG 光遗传输出（软件单脉冲），时长单位为毫秒
OG_PIN = "Y4"           # PB9

# miniscope 记录触发（普通 GPIO 高/低）
MS_RECORD_PIN = "X10"   # PB7

# 红外门控（进场检测，ExtInt FALLING，原代码注释掉，预留）
INFRARED_DETECT_PIN = "X11"   # PB4

# 压杆检测（ExtInt FALLING，原代码注释掉，预留）
PRESS_LEVER_PIN = "X12"       # PB5

# 同步信号输入（ExtInt CHANGE，原代码注释掉，预留）
SYNC_INPUT_PIN = "Y1"         # PC6

# 光功率 DAC（12-bit, 0~3.3V）
LIGHT_POWER_DAC = 1           # DAC1 = X5 (PA4)

# 板上指示 LED（原代码用 pin 13 做泵工作指示）
LED_PIN = "LED_RED"           # pyboard 红 LED

# 备用常高引脚（原代码 pin 50 常 HIGH，若有外部依赖）
SPARE_HIGH_PIN = "X9"         # PB6

# ============ 软件单脉冲共享定时器（不占用引脚 AF，仅作超时回调） ============
# 给水泵共用 TIM2（32 位），OG 用 TIM5（32 位，可覆盖 100ms+ 长脉冲）
PUMP_TIMER = 2
OG_TIMER = 5

# ============ 逻辑电平（对应原 LICK_ACTIVE/LICK_SILENCE） ============
LICK_ACTIVE = True
LICK_SILENCE = False

# ============ 协议常量契约（与 Unity 上位机严格一致，勿改下标顺序） ============
# 发送帧 typeId 映射表（下标即 typeId）
SERIAL_PRINT_TYPE = [
    "li",        # 0
    "en",        # 1
    "pr",        # 2
    "ci",        # 3
    "log",       # 4
    "echo",      # 5
    "vc",        # 6
    "cmd",       # 7
    "debugLog",  # 8
    "st",        # 9
    "si",        # 10
    "ms",        # 11
]

# 帧定界符
FRAME_START = 0xAA
FRAME_END = 0xDD
TEXT_CMD_MARK = 0x2F   # '/'

# 需要缓存的前缀（对应原 cachePrefixes）
CACHE_PREFIXES = ["li", "en", "pr"]
MAX_CACHE_ENTRIES = 64
CACHE_STRING_MAX_LEN = 24

# 状态变量索引契约（pointer_array，下标即命令里的变量序号 0~10）
STATE_ORDER = [
    "lick_mode",            # 0
    "trial",                # 1
    "trial_set",            # 2
    "now_pos",              # 3
    "lick_rec_pos",         # 4
    "INDEBUGMODE",          # 5
    "OGActiveMills",        # 6
    "miniscopeRecord",      # 7
    "waterServeWhenLick",   # 8
    "waterServeManual",     # 9
    "lightControl",         # 10
]

# 数组类型索引契约（pointerArrayType_array，命令 arr[idx]=value）
ARRAY_TYPES = ["waterServeMicros", "lick_count", "water_flush"]
ARRAY_TYPE_LENGTH = [8, 8, 8]

# 发送缓冲相关
MAX_MSG_COUNT_PER_CHUNK = 20
