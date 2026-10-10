# pyBoard Plus 引脚重规划方案（V0.2）

> 背景：原工程按 pyboard v1.1（STM32F405RG，LQFP64）规划。现改用 **01Studio pyBoard v1.1 Plus（STM32F407VGT6，LQFP100）**，
> 利用其扩展引出的 GPIO 重新分配，目标：泵/舔水组内连续、无冗余、省中断资源、留足扩展余量。
> 本文档为最新定稿，**优先级高于**《迁移评估与实现规划.md》中的旧引脚表。

---

## 一、芯片层面兼容性结论

| | 原版 pyboard v1.1 | pyBoard Plus |
|---|---|---|
| 芯片 | STM32F405RGT6 | STM32F407VGT6 |
| 封装 | LQFP64 | LQFP100 |
| Flash / RAM | 1MB / 192KB | 1MB / 192KB |

- F405/F407 共用数据手册 DS8626，Table 9 "Alternate function mapping" 按**端口+引脚号**粒度定义；
- 原有 X1~X12 / Y1~Y12 的 CPU 映射与 AF 复用（定时器/UART/SPI/ADC/DAC）在两块板上**完全一致**；
- Plus 的"扩展 GPIO"纯粹是 100 脚封装多引出了 64 脚封装没有的引脚（**PE0~PE15 经右侧内排完整引出**），不是重新映射；
- F407 多出的 Ethernet/DCMI/HASH 外设不改变已有引脚的复用功能，本项目零影响。

**板上已占用、规划时避开的资源**：

| 资源 | 占用 |
|---|---|
| PA8 | SD_SW（SD 卡插入检测） |
| PB2 | MMA_INT（加速度计中断） |
| PB5 | MMA_AVDD（加速度计供电） |
| PB3 | USR 按键 |
| X9/X10（PB6/PB7） | 加速度计 I2C1（SCL/SDA），勿做普通推挽输出；作 I2C1 通讯可用（见第五节） |
| PC8~PC12 / PD2 | SD 卡 SDIO |
| PA11/PA12 | USB OTG_FS |
| PC13 | 输出限 3mA，不用 |

---

## 二、分配原则（本次需求）

1. 给水泵 8 路、舔水 8 路各自**组内连续**（GPIO 号连续、物理相邻）；
2. 其他输出（OG、miniscope 等）不需要高精度、不需要定时器通道直驱（软件单脉冲即可）；
3. 舔水检测**每根引脚同时挂 RISING + FALLING 中断**（`IRQ_RISING_FALLING`），REACH/LEAVE 共用一根引脚，省引脚省资源；**默认全下拉**（`PULL_DOWN`，空闲钉低=LEAVE，抗悬空/长导线噪声）；
4. 不做冗余预留，只给红外、压杆留了位置；其余资源全部留给后续扩展。

---

## 三、引脚分配表（定稿，以 `config.py` 为准）

| 功能 | 引脚 | CPU | EXTI/外设 | 状态 |
|---|---|---|---|---|
| 给水泵 0~7 | E0~E7 | PE0~PE7 | GPIO 输出，软脉冲（TIM2 共享回调） | 启用 |
| 舔水 0~7 | E8~E15 | PE8~PE15 | ExtInt，RISING+FALLING 双沿（EXTI8~EXTI15），PULL_DOWN | 启用 |
| OG PWM0 | Y3 | PB8 | TIM10_CH1，硬件 PWM（承接原 OGActiveMills 定时脉冲语义，TIM5 做自动关断超时） | 启用 |
| OG PWM1 | Y4 | PB9 | TIM11_CH1，硬件 PWM（频率与 PWM0 独立，协议命令待扩展） | 启用 |
| OG 高低信号 | X1 / X2 | PA0 / PA1 | GPIO 高/低（UART4 预留随之释放） | 启用 |
| miniscope 记录触发 | Y9 | PB10 | GPIO 高/低（`MS_RECORD_PINS` 列表结构模仿 OG，当前单通道；只响应开/关、不做定时） | 启用 |
| 红外门控（进场） | Y11 | PB0 | EXTI0，双沿（高=In/低=leave）；接线后于 EXTINT_TABLE 取消注释启用 | 预留 |
| 压杆检测 | Y12 | PB1 | EXTI1，FALLING；接线后于 EXTINT_TABLE 取消注释启用 | 预留 |
| 光功率0 | X5 | PA4 | DAC1，12-bit 0~3.3V（硬件固定，无选择） | 启用 |
| 光功率1 | X6 | PA5 | DAC2，12-bit 0~3.3V（硬件固定，无选择） | 启用 |
| 泵工作指示 | LED_RED | — | 板载红 LED | 启用 |
| 备用常高 | — | — | 原 Arduino pin 50 恒高属冗余项，**不分配**（`SPARE_HIGH_PIN = None`，main.py 已兼容） | 取消 |
| 同步信号输入 | — | — | 原代码注释掉，不预留 | 取消 |
| REPL | UART6（Y1=PC6 / Y2=PC7） | — | boot.py / main.py 的 `os.dupterm` | 维持 |

**舔水语义变化**：原实现中 "lick 0 的 LEAVE 借用 lick 1 引脚 FALLING" 已废弃。
现在每根舔水引脚独立双沿：回调内读电平，高=REACH（`li:i:1`，且 `lick_count[i]++`），低=LEAVE（`li:i:0`）。
与 Unity 的帧格式不变，仅 leave 事件来源引脚变了。

### 中断配置表（`EXTINT_TABLE`，2026-09 重构，对标 Arduino `attachInterrupt`）

所有「哪个引脚挂什么中断」集中在 `config.py` 的 `EXTINT_TABLE`，一行 = 一个中断，改一行即生效：

| 列 | 取值 | 含义 |
|---|---|---|
| 引脚 | `"E8"`… | 引脚名（pyb.Pin 命名） |
| 边沿 | `rising_falling` / `rising` / `falling` / `change` | `change` 等价双沿（STM32 无独立 CHANGE） |
| 上下拉 | `up` / `down` / `none` | **默认全 `down`**（空闲钉低=LEAVE，抗悬空/长导线噪声） |
| 处理器 | `lick` / `infrared` / `press` / `sync` | main.py `INT_HANDLERS` 的键（只置标志，组帧仍由 `_poll_sensors` 完成） |
| 通道号 | lick 用 0~7；其余传 0 | 上报 `li:<ch>:<reach>` 的 ch |

- 当前 8 行舔水启用（E8~E15，双沿 + 下拉）；红外/压杆/同步行已写好但**注释**，接线后取消注释即生效，无需改 `main.py`；
- `LICK_PINS` 由表派生（单一真相源，供 af_list.py 诊断扫描）；
- **只改「上下拉」那一格**即可把某脚从 `down` 换 `up`/`none`，以匹配不同传感器极性（开漏型必须 `up`）。

**OG 重构（2026-09，二次定稿）**：**2 路硬件 PWM 线（Y3=TIM10_CH1 / Y4=TIM11_CH1，单通道定时器，频率天然独立）+ 2 路门控线（X1/X2，普通 GPIO）**；
miniscope 触发从 Y3 移至 Y9；Y10 空置；上一版的 X3/X7 PWM 脚释放。初版（Y4 单脚软件单脉冲）彻底废弃。
PWM 线默认全高，调制参数仅由 `cmd:ogpwm` 修改；门控线仅由 `cmd:ogset` 驱动；
旧渠道（变量 6 `OGActiveMills`）作用通道 0 的 PWM 线，全高语义，不改调制参数。
注意：X1/X2 改为输出后不注册 EXTI，与红外/压杆的 Y11（EXTI0）/Y12（EXTI1）预留不冲突。

### 命令文法约定（2026-09 定稿，后续扩展命令均照此）

- 新文法命令串：`<type>:<case>;<p1>;<p2>;...`：**第一个冒号前为类型**（当前仅 `cmd`，其他类型静默忽略留作扩展）；
  冒号后为命令体；命令体按 `;` split，**第一个值为 case（进 switch）**，其余为顺序参数。
- **与旧 Arduino 渠道部分兼容并存**：裸关键字、`sw=`、`arr[i]=`、`var=`、`//` 明文、帧传输全部不变；
  新文法靠“首冒号前缀 == cmd”识别，与旧串形状不冲突（旧命令串不含冒号）。
- 参数通用规则：缺失→默认值；存在但非数值→整条静默丢弃；执行成功回明文 `echo:<原命令>:echo`，
  `INDEBUGMODE>0` 时追加 `debugLog` 状态帧。
- 板型标识：上电/重握手 init 完成后、首条 `initialed:<VERSION>` 之前，明文发送两遍 `pyboardV1.1Plus`
  （`config.BOARD_BANNER`，每行带 \r\n），供上位机识别板型。
- 维护后门 `exitmain`（关键字命令，帧或 `//` 明文均可）：干净退出主循环、恢复 Ctrl-C、落回 UART6 REPL，
  供 deploy/调试接管；成功先回 `echo:exitmain:echo`。

### 光遗传命令规格（`ogpwm` / `ogset`，供 C# 端对照）

```
cmd:ogpwm;<ch>;<freqHz>;<duty%>;<timeMs>
cmd:ogset;<ch>;<timeMs>
```

**ogpwm**（作用 PWM 线 ch0=Y3/TIM10、ch1=Y4/TIM11；修改调制参数并按 time 应用）：

| 序 | 参数 | 规则 |
|---|---|---|
| 1 | ch | 0/1/-1（**-1 = 同参作用所有通道**），缺失或其他值 return |
| 2 | freq | clamp 到 1~5000 Hz；缺省 20 |
| 3 | duty | clamp 到 1~100 %；缺省 50 |
| 4 | time | 0=关断；-1=持续；>0=毫秒定时自关；缺省 0；< -1 按 0 处理 |

**ogset**（独立于 PWM，只驱动门控线 X1=ch0 / X2=ch1，不影响 Y3/Y4）：

| 序 | 参数 | 规则 |
|---|---|---|
| 1 | ch | 0/1/-1（-1 = 所有通道），否则 return |
| 2 | time | 同 ogpwm 的 time（0 关 / -1 持续 / >0 定时，缺省 0） |

**msset**（miniscope 记录开/关，**无定时**；驱动 `MS_RECORD_PINS` 列表，当前单通道 Y9）：

| 序 | 参数 | 规则 |
|---|---|---|
| 1 | ch | 0/-1（-1 = 所有通道），越界 return |
| 2 | state | 整数；0=关，非 0=开；缺失/非数值整条丢弃 |

- 定时自关分工：PWM 线用 TIM5（单实例，新脉冲覆盖旧倒计时）；门控线用独立 TIM13，互不互踩；
  ch=-1 的定时自关以“全局倒计时”标记（内部 -2）作用所有通道；**全局倒计时存续期间若下发单通道开/关命令，
  剩余倒计时移交给未被动的那路**（单实例无法同时跟踪两路）；
- 定时精度（2026-09 精确化）：弃用旧的 `freq=round(1000/ms)` 整数 Hz 法（>1s 塌缩成 1s）；
  TIM5（32 位）改 `prescaler=83999, period=ms-1`（1kHz tick）**任意时长毫秒精确**一次性闹钟；
  TIM13（16 位）≤65535ms 同法毫秒精确，>65.5s 退化 10Hz tick+计数（±100ms）；
  `time=-1` 持续模式不挂任何定时器，关断命令即时响应；
- “全高”用 PWM 100% 占空比实现（电平等价普通 GPIO 高，不切引脚 AF）；
- 旧渠道 `6=<mills>`（OGActiveMills）语义对标 Arduino：通道 0 的 PWM 线，>0 全高定时脉冲 / 0 关断 / <0 持续全高，不改调制参数；
- 其他 case 在 switch 中留空占位，未注册 case 静默 return；
- 帧字节示例（`cmd:ogpwm;0;1000;50;-1`，len=0x16=22，typeId 可沿用 cmd=7）：
  `AA 07 16 63 6D 64 3A 6F 67 70 77 6D 3B 30 3B 31 30 30 30 3B 35 30 3B 2D 31 DD 0D 0A`。

---

## 四、资源冲突核查（已通过）

- **EXTI 线**：舔水占 EXTI8~15；红外/压杆将来占 EXTI0/EXTI1；泵（GPIO 号 0~7）无中断 → 互不冲突；
- **红外/压杆为什么不用 Y9/Y10**：EXTI 线按 GPIO 编号跨端口共享，舔水 E8~E15 已占满 EXTI8~15，
  而 Y9=PB10→EXTI10、Y10=PB11→EXTI11，分别与 lick2(PE10)/lick3(PE11) 冲突。
  故中断类输入只能落在空闲编号（0/1 → Y11=PB0/Y12=PB1）；X1/X2 现为 OG 高低信号（仅输出，不注册 EXTI，不占 EXTI0/1 线）；
- **定时器**：TIM2（泵软脉冲超时）、TIM10（OG PWM0）、TIM11（OG PWM1）已被占用；TIM5 作 PWM0 定时关断的超时回调，
  **每次触发重设频率，整颗不可再挂 PWM**，其通道脚（X1~X4 的 TIM5、X1/X2/Y9/Y10 的 TIM2）永远不能做 PWM；
  TIM10/11 为单通道定时器，无残留通道；OG 改挂 TIM10/11 后 **TIM3、TIM9 重新释放**；
  完全空闲的硬件直驱资源：TIM1/3/4/8/9/12/13/14 及对应引脚通道。
- **REPL**：UART6 用 Y1=PC6/Y2=PC7，未被其他功能占用；
- **泵/舔水同占 GPIO 号 0~7 / 8~15**：分属 PE0~PE7 与 PE8~PE15，EXTI 线不共享，无冲突。

### EXTI 占用与互斥速查（后续加中断前先查此表）

EXTI 按**引脚编号**（非端口、非物理位置）分配，共 16 条，同编号跨端口互斥。
舔水 E8~E15 已占满 EXTI8~15，故 **编号 8~15 的引脚（Y3~Y10 等）永远不能再挂中断**。

| EXTI 线 | 候选（同线只能选一个） | 状态 |
|---|---|---|
| 0 | X1(PA0) / X19(PC0) / Y11(PB0) | Y11 预留给红外；X1 为 OG 输出，未注册中断，不占线 |
| 1 | X2(PA1) / X20(PC1) / Y12(PB1) | Y12 预留给压杆；X2 为 OG 输出，同上 |
| 2 | X3(PA2) / X21(PC2) | 空闲（二选一） |
| 3 | X4(PA3) / X22(PC3)（X17=PB3 也在此线，但被板载 USR 键占用） | 空闲（二选一） |
| 4 | X11(PC4) | 空闲 |
| 5 | X6(PA5) / X12(PC5) | 空闲（二选一；X6 另有 DAC2 可选） |
| 6 | X7(PA6) / Y1(PC6) | Y1 被 REPL(UART6) 占，X7 空闲（UART 不配 EXTI，不影响 X7） |
| 7 | X8(PA7) / Y2(PC7) | Y2 被 REPL 占，X8 空闲（同上，UART 不配 EXTI） |
| 8~15 | 全部被舔水占用（X18=PC13 的 EXTI13 也在其中） | 不可用 |

### 中断能力与扩展总览（2026-08 盘点）

**可自由挂中断**（EXTI 线空闲，互斥对子内各选其一）：
X3、X4、X11、X7、X8；同线候补：X21（与 X3 同 EXTI2）、X22（与 X4 同 EXTI3）、X12（与 X6 同 EXTI5）。
另有 Y11/Y12 为红外/压杆预留（EXTI0/1），接线后启用。
X1/X2 现作 OG 高低信号输出，不注册中断（EXTI 只在注册 ExtInt 时互斥，故不影响 Y11/Y12 预留）。
**REPL 不占中断线**：UART 外设不配置 EXTI，只占 Y1/Y2 引脚本身；因此 Y1/Y2 留给 REPL 时，
同线的 X7（EXTI6）、X8（EXTI7）照样可挂中断；反过来若把 REPL 从 UART6 挪走，Y1/Y2 也可挂中断（与 X7/X8 仍是同线二选一）。
注意：E0~E7（泵）虽是编号 0~7 的脚，但仅作 GPIO 输出、不注册 ExtInt，**不占用** EXTI0~7 线，不构成上述限制。
另：同一根脚做 ADC（模拟模式）时不能同时注册 ExtInt，两者按脚分工；但 ADC 不占 EXTI 线，不影响别的脚挂中断（如 X21/X22 采 ADC 与 X3/X4 挂中断互不干扰）。

**永远挂不了中断**（编号 8~15，EXTI8~15 被舔水永久占满）：
Y3~Y10、Y5~Y8 及任何编号 8~15 的脚，只能轮询输入、输出或走总线外设（SPI2/I2C2/TIM 通道等）。

**边缘/不推荐**：
- X17（PB3）：板载 USR 按键占用，且为 SWO 调试脚；EXTI3 与 X4 同线；不当普通 GPIO 用；
- X18（PC13）：输出限 3mA，且 EXTI13 已被舔水占线，仅可做弱轮询输入。

**总线与中断无关**：SPI2（Y5~Y8）、I2C1（X9/X10）均为硬件外设总线，不依赖 EXTI；
Y5~Y8 本就挂不了中断，X9/X10 的 EXTI6/7 虽空闲但 I2C 用不到 → 总线预留不受任何中断占用影响。

---

## 五、剩余扩展资源（含建议用途）

| 资源 | 建议用途 / 说明 |
|---|---|
| Y5~Y8（PB12/PB13/PB14/PB15） | **SPI(2) 总线预留**：NSS=Y5, SCK=Y6, MISO=Y7, MOSI=Y8（pyboard 官方 SPI2，四脚连续别无他选）；Y7/Y8 另有 TIM12 通道 |
| X9/X10（PB6/PB7） | **I2C(1) 预留**：SCL=X9, SDA=X10；加速度计 MMA7660（地址 0x4C）同总线，加挂器件避开此地址即可 |
| X3（PA2） | 空闲（上一版 OG PWM 释放）；TIM9_CH1 可 PWM（TIM2/5 通道已锁）；EXTI2 |
| X7（PA6） | 空闲（上一版 OG PWM 释放）；TIM3_CH1/TIM13 可 PWM；EXTI6 |
| Y10（PB11） | 空置；普通 GPIO；EXTI11 被舔水占，仅输出/轮询；带 TIM2_CH4（已锁）、I2C2 |
| Y1/Y2（PC6/PC7） | 现被 REPL(UART6) 占用，必要时可挪走 |
| X4（PA3） | 空闲；带 TIM9_CH2（TIM9 已随 OG 迁走重新空闲，可自由用）、TIM5 通道已锁；EXTI3 |
| X6（PA5） | 空闲；带 TIM8_CH1 可 PWM；DAC2 可选（二选一）；EXTI5 |
| X8（PA7） | 空闲；带 TIM3_CH2（TIM3 已随 OG 迁走重新空闲，可自由用）、TIM14；EXTI7 |
| X11/X12（PC4/PC5） | 空闲，普通 GPIO；无定时器通道（不能 PWM）；EXTI4 / EXTI5（与 X6 同线） |
| X19~X22（PC0~PC3） | **空闲，未分配**；原版板为 shielded 模拟输入（焊盘包地屏蔽，适合高阻抗传感器）；ADC123_IN10~13；**无定时器通道，不能 PWM**；X19/X20 的 EXTI0/1 与红外/压杆预留同线，X21/X22 的 EXTI2/3 与 X3/X4 同线；Plus 板是否保留屏蔽铜皮需看实物 |
| X17（PB3） | 板载 USR 按键 + SWO 调试脚，不分配 |
| X18（PC13） | 输出限 3mA 且 EXTI13 被舔水占线，仅可弱轮询输入，不分配 |

---

## 六、代码配套改动状态

| 文件 | 是否改动 | 内容 |
|---|---|---|
| `config.py` | ✅ 已改 | 新引脚表；红外/压杆预留；备用常高/同步取消；OG 二次定稿为 Y3/Y4 PWM 线 + X1/X2 门控线，miniscope 移至 Y9，Y10 空置；ogpwm 钳位常量（freq 1~5000、duty 1~100）与默认值（20Hz/50%）；新增 `OG_GATE_TIMER=13`；`MS_RECORD_PIN` 改列表 `MS_RECORD_PINS` |
| `main.py` | ✅ 已改（本轮 MS 重构+OG 精确化） | 舔水 8 路双沿单回调；OG 类 PWM 线/门控线模型（含 ch=-1 全通道）；OG 定时改 prescaler/period 精确一次性闹钟（TIM13 >65.5s 退化 10Hz 计数）；新增 MS 类（多通道结构、只开关）与 `cmd:msset` case；旧渠道变量 6/7 分别映射 OG ch0 全高 / MS ch0；`micropython.kbd_intr(-1)`；`exitmain` 后门 |
| `comm.py` | ❌ 无需 | 纯通讯层，与引脚无关 |
| `boot.py` | ❌ 无需 | 仅 REPL 迁移到 UART6 |
| `af_list.py` | ❌ 无需 | 开发辅助脚本（非部署）；新方案泵/舔水不依赖定时器通道，无需再做 AF 建议 |

---

## 七、上板注意事项

1. **引脚命名**：`Pin("E0")` 是否可用取决于 Plus 固件的引脚定义表。若报 `ValueError`：
   - 改用 CPU 名：`pyb.Pin.cpu.E0`；或
   - 自定义别名：`pyb.Pin.dict({"E0": pyb.Pin.cpu.E0, ...})`。
   只需改 `config.py` 里的名字（或统一加一段别名注册），逻辑代码不受影响。
2. **固件**：必须使用 01Studio 面向 pyBoard Plus（STM32F407VGT6）的 MicroPython 固件，勿刷原版 PYBV11（F405RG）固件。
3. **验证顺序建议**：先跑通 `Pin("E0")` 类命名的冒烟测试 → 示波器/万用表验证泵脉冲（`pulse_width` 等价 10ms 高）→ 舔水双沿上报（`li:i:1` / `li:i:0`）→ 握手联调。
4. **TIM10/TIM11 上板验证**：`Timer(10)/Timer(11)` 属常规 16 位定时器，MicroPython stm32 端口普遍支持，但 Plus 固件的引脚表是否把 Y3→TIM10_CH1、Y4→TIM11_CH1 编进去了需上板确认；若 `Timer(10).channel(1, Timer.PWM, pin=...)` 报错，备选：两路改挂 TIM4（Y3=TIM4_CH3、Y4=TIM4_CH4，代价是两路同频）。
5. **带内 0x03 危害与对策（2026-09 事故定案）**：STM32 MicroPython 的 USB VCP 接收路径对每个入站字节做中断字符（Ctrl-C=0x03）检查，与 dupterm 是否在 UART6 无关；
   上位机帧中作为普通数据出现的 0x03（典型：`6=0`/`7=0` 等 3 字节 content 的**长度字节**，Unity 退出时 CloseDevices 必发）会异步触发 KeyboardInterrupt，打穿 main.py 并软复位，且该 0x03 字节被吞导致当帧丢失。
   对策：main.py 启动即 `micropython.kbd_intr(-1)`，0x03 回归普通数据；维护中断改用明文/帧命令 `exitmain`（干净退出主循环落回 UART6 REPL 并恢复 Ctrl-C）。
   **deploy 流程变更**：`deploy*.ps1` 开头的 0x03 打断步骤对业务运行中的板子已失效，改为先发 `//exitmain` 等 REPL 提示符出现后再传文件。
