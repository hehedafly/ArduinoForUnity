# hw.py — 硬件驱动层：DAC / 输出引脚 + Pump/OG/MS 类与实例。
# 从 main.py 拆出，目的：减小 main.py 单文件体积，避免板载编译 main.py 时编译峰值吃光 ~67KB 堆而 OOM。
# 只依赖 config + pyb，不引用 main.py 的业务全局量（state / lick_flags 等），故可干净拆分。
# 注意：本文件内不写 docstring（部署脚本会把非 ASCII 转成 '?'，docstring 在板上无价值且白占堆）。
import config
from pyb import Pin, Timer, DAC

# ---- 输出引脚 ----
pump_pins = [Pin(name, Pin.OUT_PP) for name in config.PUMP_PINS]
led_pin = Pin(config.LED_PIN, Pin.OUT_PP)
try:
    spare_high_pin = Pin(config.SPARE_HIGH_PIN, Pin.OUT_PP)
except Exception:
    spare_high_pin = None

# ---- 触摸面板复位引脚（未配置为 None；低有效，空闲保持高）----
tp_reset_pin = None
if config.TOUCHPANNEL_RESET_PIN is not None:
    tp_reset_pin = Pin(config.TOUCHPANNEL_RESET_PIN, Pin.OUT_PP)
    tp_reset_pin.high()

# ---- DAC（光功率 12-bit 0~3.3V，2 路独立：DAC1=X5/PA4 光功率0、DAC2=X6/PA5 光功率1）----
light_dacs = [DAC(i, bits=12) for i in config.LIGHT_POWER_DACS]

def set_light_power(v, ch=-1):
    # ch: 0/1 = 单路；-1 = 全部（旧渠道 10=lightControl 走全部，保持"单一光功率"原语义）
    v = int(v)
    v = config.LIGHT_POWER_FULL if v > config.LIGHT_POWER_FULL else (0 if v < 0 else v)
    if ch == -1:
        for d in light_dacs:
            d.write(v)
    elif 0 <= ch < len(light_dacs):
        light_dacs[ch].write(v)

# ---- 给水泵：软件单脉冲，8 路共用 1 个定时器，脉冲期间不重入（对标 Arduino active_pin 保护）----
class Pump:
    def __init__(self):
        self.pins = [Pin(name, Pin.OUT_PP) for name in config.PUMP_PINS]
        self.tim = Timer(config.PUMP_TIMER)
        self.active = -1   # 当前触发泵号：-1 空闲，-2 全部

    def _done(self, t):
        if self.active == -2:
            for p in self.pins:
                p.low()
        elif 0 <= self.active < len(self.pins):
            self.pins[self.active].low()
        self.active = -1
        self.tim.callback(None)

    def _arm(self, micros):
        # freq = 1e6/micros (Hz)，回调在 micros 微秒后触发一次
        self.tim.init(freq=max(1, round(1000000 / micros)), callback=self._done)

    def set(self, idx, micros):
        if self.active != -1:
            return   # 对标 Arduino：当前脉冲未结束时不设置其它泵
        if 0 <= idx < len(self.pins):
            self.pins[idx].high()
            self.active = idx
            self._arm(micros)

    def set_all(self, micros):
        if self.active != -1:
            return
        for p in self.pins:
            p.high()
        self.active = -2
        self._arm(micros)

    def off_all(self):
        for p in self.pins:
            p.low()
        self.active = -1
        self.tim.callback(None)

# ---- OG 光遗传：PWM 线 2 路(Y3/Y4, TIM10/TIM11) + 门控线 2 路(X1/X2) ----
# PWM 线默认全高，调制参数仅由 cmd:ogpwm 改；定时自关用 TIM5(单实例，新脉冲覆盖旧倒计时)。
# 门控线仅由 cmd:ogset 驱动；定时自关用独立 TIM13，不与 TIM5 互踩。
# 旧渠道(变量6 OGActiveMills)走 legacy_set：通道0 的 PWM 线全高语义，不改调制参数。
class OG:
    def __init__(self):
        self.pwm = []   # [[tim_id, ch_num, pin_name, tim, ch], ...]
        for pin_name, tim_id, ch_num in config.OG_PWM_PINS:
            tim = Timer(tim_id, freq=config.OG_PWM_DEFAULT_FREQ)
            ch = tim.channel(ch_num, Timer.PWM, pin=Pin(pin_name))
            ch.pulse_width_percent(0)
            self.pwm.append([tim_id, ch_num, pin_name, tim, ch])
        self.gates = [Pin(name, Pin.OUT_PP) for name in config.OG_GPIO_PINS]
        for p in self.gates:
            p.low()
        self.tim_off = Timer(config.OG_TIMEOUT_TIMER)    # PWM 线定时自关
        self.tim_gate = Timer(config.OG_GATE_TIMER)      # 门控线定时自关
        self._off_idx = -1        # TIM5 倒计时当前作用通道，-1=无，-2=全通道
        self._gate_off_idx = -1   # TIM13 倒计时当前作用通道
        self._gate_remain = 0     # TIM13 >65.5s 退化模式的 10Hz 计数余量

    # ---- PWM 线 ----
    def _pwm_ch(self, i):
        return self.pwm[i][4]

    def _pwm_targets(self, idx):
        return [0, 1] if idx == -1 else [idx]   # idx=-1 表示所有通道

    def _pwm_mod_one(self, i, freq, duty):
        tim_id, ch_num, pin_name, tim, ch = self.pwm[i]
        tim.init(freq=freq)   # init 会复位通道，需重建
        ch = tim.channel(ch_num, Timer.PWM, pin=Pin(pin_name))
        self.pwm[i] = [tim_id, ch_num, pin_name, tim, ch]
        ch.pulse_width_percent(duty)

    def _cancel_off(self, idx):
        if self._off_idx == idx or (idx == -1 and self._off_idx != -1):
            self.tim_off.callback(None)
            self._off_idx = -1
        elif self._off_idx == -2 and idx in (0, 1):
            self._off_idx = 1 - idx   # 全局倒计时在，只操作单通道：倒计时移交给未被动的那路

    def pwm_mod(self, idx, freq, duty):
        for i in self._pwm_targets(idx):
            self._pwm_mod_one(i, freq, duty)

    def pwm_full(self, idx):
        # 全高(100% 占空比，电平等价普通 GPIO 高，不切引脚 AF)
        for i in self._pwm_targets(idx):
            self._pwm_ch(i).pulse_width_percent(100)
        self._cancel_off(idx)

    def pwm_off(self, idx):
        for i in self._pwm_targets(idx):
            self._pwm_ch(i).pulse_width_percent(0)
        self._cancel_off(idx)

    def _arm_off(self, idx, ms):
        self._off_idx = -2 if idx == -1 else idx   # -2 = 倒计时作用所有通道
        # TIM5 为 32 位定时器：1kHz tick（84MHz/84000），period=ms-1 毫秒精确一次性闹钟，任意时长
        self.tim_off.init(prescaler=83999, period=ms - 1, callback=self._pulse_done)

    def _pulse_done(self, t):
        idx = self._off_idx
        self._off_idx = -1
        self.tim_off.callback(None)
        targets = [0, 1] if idx == -2 else ([idx] if 0 <= idx < len(self.pwm) else [])
        for i in targets:
            self._pwm_ch(i).pulse_width_percent(0)

    def pwm_timed(self, idx, freq, duty, ms):
        self.pwm_mod(idx, freq, duty)
        self._arm_off(idx, ms)

    def pwm_full_timed(self, idx, ms):
        self.pwm_full(idx)
        self._arm_off(idx, ms)

    # ---- 门控线 ----
    def _cancel_gate(self, idx):
        if self._gate_off_idx == idx or (idx == -1 and self._gate_off_idx != -1):
            self.tim_gate.callback(None)
            self._gate_off_idx = -1
        elif self._gate_off_idx == -2 and idx in (0, 1):
            self._gate_off_idx = 1 - idx

    def gate_set(self, idx, on):
        for i in self._pwm_targets(idx):
            self.gates[i].high() if on else self.gates[i].low()
        if not on:
            self._cancel_gate(idx)

    def _gate_done(self, t):
        idx = self._gate_off_idx
        self._gate_off_idx = -1
        self.tim_gate.callback(None)
        targets = [0, 1] if idx == -2 else ([idx] if 0 <= idx < len(self.gates) else [])
        for i in targets:
            self.gates[i].low()

    def gate_timed(self, idx, ms):
        # 门控打开，ms 后自动关(TIM13 独立倒计时)；ms<=65535 毫秒精确，>65535 退化 10Hz 计数(±100ms)
        for i in self._pwm_targets(idx):
            self.gates[i].high()
        self._gate_off_idx = -2 if idx == -1 else idx
        if ms <= 65535:
            self.tim_gate.init(prescaler=83999, period=ms - 1, callback=self._gate_done)
        else:
            self._gate_remain = ms // 100
            self.tim_gate.init(freq=10, callback=self._gate_tick)

    def _gate_tick(self, t):
        self._gate_remain -= 1
        if self._gate_remain <= 0:
            self._gate_done(t)

    # ---- 旧渠道（变量 6 OGActiveMills）：通道 0 的 PWM 线全高语义，不改调制参数 ----
    def legacy_set(self, mills):
        if mills > 0:
            self.pwm_full_timed(0, mills)
        elif mills == 0:
            self.pwm_off(0)
        else:   # <0：持续全高
            self.pwm_full(0)

    def off(self):
        for row in self.pwm:
            row[4].pulse_width_percent(0)
        for p in self.gates:
            p.low()
        self.tim_off.callback(None)
        self.tim_gate.callback(None)
        self._off_idx = -1
        self._gate_off_idx = -1

# ---- miniscope 记录触发：多通道结构（当前 1 路 Y9），只响应开/关，不做定时 ----
class MS:
    def __init__(self):
        self.pins = [Pin(n, Pin.OUT_PP) for n in config.MS_RECORD_PINS]
        for p in self.pins:
            p.low()

    def _targets(self, idx):
        return list(range(len(self.pins))) if idx == -1 else [idx]

    def set(self, idx, on):
        for i in self._targets(idx):
            self.pins[i].high() if on else self.pins[i].low()

    def off_all(self):
        for p in self.pins:
            p.low()

# ---- 实例（import 时创建，等价于原 main.py 模块级实例化）----
pump = Pump()
og = OG()
ms = MS()
