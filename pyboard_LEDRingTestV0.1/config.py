# config.py — 集中配置：引脚映射 + 协议常量契约
# 所有引脚/NUM 改动都集中在这里，改一行即可生效。

# ============ 版本（用于握手 initialed:<VERSION>） ============
VERSION = "V2.5"

# 板型标识：init 完成后、握手 initialed 之前明文发送两遍（供上位机识别板型）
BOARD_BANNER = "pyboardV1.1Plus"

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

# ============ 引脚映射（pyBoard Plus 重规划：扩展区 PE 口集中分配） ============
# 设计原则：
#   - 泵/舔水/其它输出集中到 Plus 板扩展引出的 PE 口，组内 GPIO 号连续、物理相邻。
#   - 给水为「软件单脉冲」（共享定时器回调），仅需 GPIO，不占定时器通道。
#   - 中断集中在下方 EXTINT_TABLE：一行 = 一个引脚挂什么中断，默认全下拉；舔水双沿读电平判 REACH/LEAVE。
#   - PE 口 16 脚全部用于泵+舔水；OG 光遗传 4 脚（Y3/Y4 硬件 PWM + X1/X2 高低信号）；
#     miniscope 占 Y9；Y10 空置；Y11/Y12 留给红外/压杆；X3~X8、Y5~Y8 保留，留作后续扩展。
#   - 板上占用避开：PA8=SD_SW, PB2=MMA_INT, PB5=MMA_AVDD, PB3=USR。
#   - 若固件不认 "E0" 类名字，可改用 pyb.Pin.cpu.E0 或自定义别名（pyb.Pin.dict）。

# ============ 中断配置表（对标 Arduino attachInterrupt：一行 = 一个引脚挂什么中断） ============
# 列: (引脚, 边沿, 上下拉, 处理器, 通道号)
#   边沿:   "rising_falling" | "rising" | "falling" | "change"(=双沿)
#   上下拉: "up" | "down" | "none" —— 默认全用 "down"（空闲钉低=LEAVE，抗悬空/长导线噪声）
#   处理器: main.py INT_HANDLERS 的键 —— "lick" | "infrared" | "press" | "sync"
#   通道号: lick 用 0~7（上报 li:<ch>:<reach>）；infrared/press/sync 传 0
# 改一行即改「哪个引脚挂什么中断」；整行注释即停用；不写即不注册。
EXTINT_TABLE = [
    # 8 路舔水：双沿，回调读电平 高=REACH / 低=LEAVE（PE8~PE15, EXTI8~EXTI15）
    ("E8",  "rising_falling", "down", "lick", 0),
    ("E9",  "rising_falling", "down", "lick", 1),
    ("E10", "rising_falling", "down", "lick", 2),
    ("E11", "rising_falling", "down", "lick", 3),
    ("E12", "rising_falling", "down", "lick", 4),
    ("E13", "rising_falling", "down", "lick", 5),
    ("E14", "rising_falling", "down", "lick", 6),
    ("E15", "rising_falling", "down", "lick", 7),
    # 预留：接线后取消注释即生效（引脚名与下方 INFRARED_DETECT_PIN / PRESS_LEVER_PIN 一致）
    # ("Y11", "rising_falling", "down", "infrared", 0),   # 红外 en:<trial>:In(高) / :leave(低)
    # ("Y12", "falling",        "down", "press",    0),   # 压杆 pr:<trial>
    # ("Y?",  "change",         "down", "sync",     0),   # 同步 si:sync
]

# 从表派生舔水引脚名列表（单一真相源；供 af_list.py 等旧引用使用）
LICK_PINS = [row[0] for row in EXTINT_TABLE if row[3] == "lick"]

# 8 路给水泵（输出，软件单脉冲，共享定时器回调）—— PE0~PE7 连续 8 引脚
PUMP_PINS = [
    "E0",   # pump 0, PE0
    "E1",   # pump 1, PE1
    "E2",   # pump 2, PE2
    "E3",   # pump 3, PE3
    "E4",   # pump 4, PE4
    "E5",   # pump 5, PE5
    "E6",   # pump 6, PE6
    "E7",   # pump 7, PE7
]

# 给水泵默认时长（微秒），对应原 waterServeMicros
WATER_SERVE_MICROS = [20000] * 8

# OG 光遗传：PWM 线 2 路（Y3/Y4，TIM10/TIM11 单通道定时器，频率天然独立）+ 门控线 2 路（X1/X2）。
# PWM 线默认全高，调制参数仅由 cmd:ogpwm 命令修改；定时自关用 TIM5（单实例，新脉冲覆盖旧倒计时）。
# 门控线仅由 cmd:ogset 驱动，定时自关用独立 TIM13，不与 TIM5 互踩。
# 旧渠道（变量 6 OGActiveMills）作用通道 0 的 PWM 线，全高语义，不改调制参数。
OG_PWM_PINS = [
    ("Y3", 10, 1),  # PB8, TIM10_CH1（PWM0）
    ("Y4", 11, 1),  # PB9, TIM11_CH1（PWM1）
]
OG_PWM_DEFAULT_FREQ = 20      # Hz，ogpwm 缺省 freq 参数时的默认值（光遗传脉冲串常用 20Hz）
OG_PWM_DEFAULT_DUTY = 50      # %，ogpwm 缺省 duty 参数时的默认值

# ogpwm 命令参数钳位范围：freq 1~5000Hz，duty 1~100%
OG_PWM_FREQ_MIN = 1
OG_PWM_FREQ_MAX = 5000
OG_PWM_DUTY_MIN = 1
OG_PWM_DUTY_MAX = 100

# OG 门控线 2 路（ogset 开/关：高=开，低=关；与 PWM 线独立，不改调制参数）
OG_GPIO_PINS = [
    "X1",   # PA0, 门控 0
    "X2",   # PA1, 门控 1
]

# miniscope 记录触发（普通 GPIO 高/低；模仿 OG 的多通道列表结构，当前单通道；只响应开/关、不做定时）
MS_RECORD_PINS = [
    "Y9",   # PB10, MS 0（Y10 空置，未分配）
]

# 红外门控（进场检测；已预留引脚，接线后在上方 EXTINT_TABLE 取消对应行注释即启用）
INFRARED_DETECT_PIN = "Y11"   # PB0, EXTI0

# 压杆检测（ExtInt FALLING，原代码未启用；已预留引脚）
PRESS_LEVER_PIN = "Y12"       # PB1, EXTI1

# 同步信号输入（ExtInt CHANGE，原代码注释掉，不再预留）
SYNC_INPUT_PIN = None

# 光功率 DAC（12-bit, 0~3.3V）
LIGHT_POWER_DAC = 1           # DAC1 = X5 (PA4)

# 板上指示 LED（原代码用 pin 13 做泵工作指示）
LED_PIN = "LED_RED"           # pyboard 红 LED

# 备用常高引脚：原 Arduino pin 50 恒高的冗余项，按需不分配；确有外部依赖时再从保留引脚中挑一根即可恢复（main.py 已兼容 None）
SPARE_HIGH_PIN = None

# ============ 软件单脉冲共享定时器（不占用引脚 AF，仅作超时回调） ============
# 给水泵共用 TIM2（32 位）；TIM5 作 OG PWM 线定时自关超时源；TIM13 作 OG 门控线定时自关超时源（均仅回调，不占通道）。
PUMP_TIMER = 2
OG_TIMEOUT_TIMER = 5
OG_GATE_TIMER = 13

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
