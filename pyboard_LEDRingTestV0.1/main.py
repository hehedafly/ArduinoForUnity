# main.py — 主程序（对标原 setup() + loop()）
#
# 职责：硬件驱动（GPIO/ExtInt/DAC/定时器 one-pulse）+ 状态与业务逻辑 + 命令解析 + 主循环。
# 通讯层（帧编解码/收发/握手/缓冲）见 comm.py，配置见 config.py，启动项见 boot.py。

import config
import comm
import os
import pyb
# 每次复位（含软复位）都把 REPL 挪到 UART6，防止 REPL 抢 USB VCP（软复位不会重跑 boot.py）
os.dupterm(pyb.UART(6, 115200))
from pyb import Pin, ExtInt, DAC, Timer
from utime import ticks_us, sleep_ms

# ======================================================================
# 一、硬件驱动（GPIO / ExtInt / DAC / 定时器 one-pulse）
# ======================================================================

# ---- 输出引脚 ----
pump_pins = [Pin(name, Pin.OUT_PP) for name in config.PUMP_PINS]
og_pin = Pin(config.OG_PIN, Pin.OUT_PP)
ms_record_pin = Pin(config.MS_RECORD_PIN, Pin.OUT_PP)
led_pin = Pin(config.LED_PIN, Pin.OUT_PP)
try:
    spare_high_pin = Pin(config.SPARE_HIGH_PIN, Pin.OUT_PP)
except Exception:
    spare_high_pin = None

# ---- DAC（光功率 12-bit 0~3.3V） ----
light_dac = DAC(config.LIGHT_POWER_DAC, bits=12)

def set_light_power(v):
    v = int(v)
    v = 4095 if v > 4095 else (0 if v < 0 else v)
    light_dac.write(v)

# ---- 传感器中断标志 ----
lick_flags = [0] * 8
lick_ticks = [0] * 8
lick_count = [0] * 8
_lick_dir = [True] * 8        # True=REACH(RISING), False=LEAVE(FALLING)
infrared_flags = [0, 0]
press_flag = [0]
sync_flag = [0]

def _mk_lick_cb(i):
    """lick i 的 REACH 中断：各自计数 lick_count[i]++，方向 REACH。"""
    def cb(line):
        lick_count[i] += 1
        lick_flags[i] = 1
        lick_ticks[i] = ticks_us()
        _lick_dir[i] = True
    return cb

def _lick0_leave_cb(line):
    """lick 0 的 LEAVE：原代码 LickReportInInterrupt0_leave 报告 "li:0:0"，
    绑定在 lick 1 引脚(25)的 FALLING。计数落到 lick_count[0]（各自计数原则下，
    这是 lick 0 的事件）。"""
    lick_count[0] += 1
    lick_flags[0] = 1
    lick_ticks[0] = ticks_us()
    _lick_dir[0] = False

def _infrared_cb(line):
    infrared_flags[0] = 1

_extints = []

def _setup_extints():
    # lick 0: RISING (REACH)
    _extints.append(ExtInt(config.LICK_PINS[0], ExtInt.IRQ_RISING, Pin.PULL_UP, _mk_lick_cb(0)))
    # lick 0 的 LEAVE：绑定在 lick 1 引脚(对应原 readLickPins[1]=25)的 FALLING，
    # 报告 "li:0:0"，与 LickReportInInterrupt0_leave 完全对齐。
    _extints.append(ExtInt(config.LICK_PINS[1], ExtInt.IRQ_FALLING, Pin.PULL_UP, _lick0_leave_cb))
    # lick 2~7 预留（原代码注释掉）
    # 红外门控（进场 FALLING）
    try:
        _extints.append(ExtInt(config.INFRARED_DETECT_PIN, ExtInt.IRQ_FALLING, Pin.PULL_UP, _infrared_cb))
    except Exception:
        pass

# ---- 软件单脉冲（共享定时器回调，对标 Arduino DueTimer 单一 pumpTimer） ----
# 给水/OG 输出"单次脉冲"：置高引脚 + 共享定时器在 micros 微秒后回调拉低。
# 不是周期 PWM 波形；对引脚无定时器复用约束，故给水泵可在 Y5~Y12 完全连续排列。
# 精度实测：回调下降沿约 +30~40µs 中断延迟（给水 10ms / OG 100ms 场景足够）。

class Pump:
    """8 路给水泵共用 1 个定时器，脉冲期间不重入（对标 Arduino 的 active_pin 保护）。"""
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

class OG:
    """OG 光遗传输出：独立一个定时器，时长单位毫秒；>0 单脉冲，0 关断，<0 持续高。"""
    def __init__(self):
        self.pin = Pin(config.OG_PIN, Pin.OUT_PP)
        self.tim = Timer(config.OG_TIMER)
        self.pin.low()

    def _done(self, t):
        self.pin.low()
        self.tim.callback(None)

    def set(self, mills):
        if mills > 0:
            self.pin.high()
            self.tim.init(freq=max(1, round(1000 / mills)), callback=self._done)
        elif mills == 0:
            self.pin.low()
            self.tim.callback(None)
        else:   # <0：持续拉高
            self.pin.high()
            self.tim.callback(None)

    def off(self):
        self.pin.low()
        self.tim.callback(None)

pump = Pump()
og = OG()

# ======================================================================
# 二、状态与业务逻辑（对标 TrialStart/TrialEnd/init_by_PC）
# ======================================================================

state = {
    "waiting": 1, "lick_mode": 0, "trial": 0, "trial_set": 0,
    "now_pos": -1, "pre_pos": -1, "lick_rec_pos": -1,
    "tempTrialStatusMark": -1, "waterServeWhenLick": 0,
    "waterServeManual": -1, "OGActiveMills": 100, "miniscopeRecord": 0,
    "INDEBUGMODE": 0, "lightControl": 0, "tempLickPos": -1,
    "lickEndThresholdMills": -1, "autoStressTest": 0,
}
water_flush = [0] * 8

def trial_start():
    state["waiting"] = 0
    for i in range(8):
        lick_count[i] = 0
    if state["lick_mode"] == 0:
        if state["trial_set"] == 1 and state["now_pos"] != -1:
            pump.set(state["now_pos"], config.WATER_SERVE_MICROS[state["now_pos"]])
            state["trial_set"] = 0
    return 1

def trial_end():
    state["waiting"] = 1
    state["now_pos"] = -1
    state["waterServeWhenLick"] = 0
    return 1

def init_by_pc(reset_connect_status=True):
    state.update({
        "waiting": 1, "lick_mode": 0, "trial": 0, "trial_set": 0,
        "now_pos": -1, "pre_pos": -1, "lick_rec_pos": -1,
        "tempTrialStatusMark": -1, "waterServeWhenLick": 0,
        "waterServeManual": -1, "OGActiveMills": 100, "miniscopeRecord": 0,
        "INDEBUGMODE": 0, "tempLickPos": -1, "lickEndThresholdMills": -1,
        "autoStressTest": 0,
    })
    for i in range(8):
        water_flush[i] = 0
        lick_count[i] = 0
    pump.off_all()
    og.off()
    ms_record_pin.low()
    set_light_power(0)
    comm.send_buffer.clear()
    if reset_connect_status:
        comm.set_handshake(False)
    return 1

def print_status(head=""):
    s = state
    msg = (" lick_mode{0}, tria {1}, tempTrialStatusMark{2}, now_pos{3}, INDEBUGMODE{4}, "
           "waiting{5}, OGAMills{6}, MSRecord{7}, waterServeManual{8}, lightCon{9} ").format(
        s["lick_mode"], s["trial"], s["tempTrialStatusMark"], s["now_pos"],
        s["INDEBUGMODE"], s["waiting"], s["OGActiveMills"], s["miniscopeRecord"],
        s["waterServeManual"], s["lightControl"])
    comm.push_logical("debugLog:" + head + msg)

def print_array_status(head=""):
    parts = [
        "waterServeMicros: " + ", ".join(str(x) for x in config.WATER_SERVE_MICROS),
        "lick_count: " + ", ".join(str(x) for x in lick_count),
        "water_flush: " + ", ".join(str(x) for x in water_flush),
    ]
    comm.push_logical("debugLog:" + head + "; ".join(parts))

# ======================================================================
# 三、命令解析（对标 commandParse）
# ======================================================================

def command_parse(cmd):
    cmd = cmd.replace("\n", "").replace("\r", "")
    s = state

    if cmd == "check":
        print_status(); return
    if cmd == "checkArray":
        print_array_status(); return
    if cmd == "forceinit":
        init_by_pc(); return
    if cmd == "clear":
        init_by_pc(False); return
    if cmd == "autoStressTest":
        comm.push_logical("debugLog:autoStressTest start")
        s["autoStressTest"] = 1; return
    if cmd == "autoStressTestStop":
        s["autoStressTest"] = 0; return

    eq = cmd.find("=")
    if eq > 0:
        lhs = cmd[:eq]
        rhs = cmd[eq + 1:]

        # sw=<泵号>
        if lhs == "sw":
            try:
                idx = int(rhs)
                if 0 <= idx < len(config.PUMP_PINS):
                    pump.set(idx, config.WATER_SERVE_MICROS[idx])
            except ValueError:
                pass
            return

        # arr[idx]=value
        lb = lhs.find("[")
        if lb > 0:
            rb = lhs.find("]")
            if rb > 0:
                try:
                    arr_var = int(lhs[:lb])
                    arr_idx = int(lhs[lb + 1:rb])
                    val = int(rhs)
                except ValueError:
                    return
                if not (0 <= arr_var < len(config.ARRAY_TYPES)):
                    return
                if not (0 <= arr_idx < config.ARRAY_TYPE_LENGTH[arr_var]):
                    return
                type_name = config.ARRAY_TYPES[arr_var]
                if type_name == "water_flush":
                    newv = 1 if (val >= 1 and water_flush[arr_idx] == 0) else 0
                    water_flush[arr_idx] = newv
                    pump_pins[arr_idx].high() if newv else pump_pins[arr_idx].low()
                elif type_name == "lick_count":
                    lick_count[arr_idx] = val
                elif type_name == "waterServeMicros":
                    config.WATER_SERVE_MICROS[arr_idx] = val
                # 对标原代码：echo 走 CUSTOM_SERIAL.println 明文，非帧
                comm.send_plain_line("echo:{0}:echo".format(cmd))
                if s["INDEBUGMODE"] > 0:
                    print_array_status()
                return

        # var=value
        else:
            try:
                var_idx = int(lhs)
                val = int(rhs)
            except ValueError:
                return
            if 0 <= var_idx < len(config.STATE_ORDER):
                var_name = config.STATE_ORDER[var_idx]
                s[var_name] = val
                # 对标原代码：echo 走 CUSTOM_SERIAL.println 明文，非帧
                comm.send_plain_line("echo:{0}:echo".format(cmd))
                if s["INDEBUGMODE"] > 0:
                    print_status()
                if var_name == "trial_set":
                    s["tempTrialStatusMark"] = val
                elif var_name == "OGActiveMills":
                    og.set(val)
                elif var_name == "miniscopeRecord":
                    ms_record_pin.high() if val != 0 else ms_record_pin.low()
                elif var_name == "now_pos":
                    s["pre_pos"] = s["now_pos"]
                elif var_name == "lightControl":
                    v = 4096 if val > 4096 else (0 if val < 0 else val)
                    s["lightControl"] = v
                    set_light_power(v)
            return

# ======================================================================
# 四、主循环（对标 loop）
# ======================================================================

def _poll_sensors():
    for i in range(8):
        if lick_flags[i]:
            lick_flags[i] = 0
            reach = 1 if _lick_dir[i] else 0
            comm.push_logical("{0}:{1}:{2}".format(config.SERIAL_PRINT_TYPE[0], i, reach))
    if infrared_flags[0]:
        infrared_flags[0] = 0
        comm.push_logical("{0}:{1}:In".format(config.SERIAL_PRINT_TYPE[1], state["trial"]))
    if infrared_flags[1]:
        infrared_flags[1] = 0
        comm.push_logical("{0}:{1}:leave".format(config.SERIAL_PRINT_TYPE[1], state["trial"]))
    if press_flag[0]:
        press_flag[0] = 0
        comm.push_logical("{0}:{1}".format(config.SERIAL_PRINT_TYPE[2], state["trial"]))
    if sync_flag[0]:
        sync_flag[0] = 0
        comm.push_logical("{0}:sync".format(config.SERIAL_PRINT_TYPE[10]))

def _poll_trial_status():
    s = state
    if s["tempTrialStatusMark"] != -1:
        mark = s["tempTrialStatusMark"]
        if mark == 1:
            trial_start()
            if s["autoStressTest"] > 0:
                sleep_ms(100)
                comm.push_logical("{0}:-4:1".format(config.SERIAL_PRINT_TYPE[0]))
        else:
            if mark == 2 and s["now_pos"] != -1:
                pump.set(s["now_pos"], config.WATER_SERVE_MICROS[s["now_pos"]])
                trial_end()
            elif mark == 3 and s["now_pos"] != -1:
                s["waterServeWhenLick"] = 1
            elif mark == 0:
                trial_end()
        s["tempTrialStatusMark"] = -1

def _poll_water_manual():
    s = state
    if s["waterServeManual"] != -1:
        if s["waterServeManual"] == -2:
            if s["pre_pos"] != -1:
                s["waterServeManual"] = s["pre_pos"]
        if -1 < s["waterServeManual"] < len(config.PUMP_PINS):
            pump.set(s["waterServeManual"], config.WATER_SERVE_MICROS[s["waterServeManual"]])
        s["waterServeManual"] = -1

def loop():
    if not comm.handshake_done:
        comm.handshake_loop()

    _poll_sensors()
    _poll_trial_status()
    _poll_water_manual()

    # 发送队列 -> 串口
    _count = 0
    while not comm.send_buffer.is_empty() and _count < config.MAX_MSG_COUNT_PER_CHUNK:
        chunk = comm.send_buffer.pop_chunk(512)
        if chunk:
            comm.send_bytes(chunk)
            _count += 1

    # 接收
    comm.process_read(command_parse)

# ======================================================================
# 启动
# ======================================================================

def setup():
    comm.open_serial()
    # 输出初始化
    for p in pump_pins:
        p.low()
    og_pin.low()
    ms_record_pin.low()
    led_pin.low()
    if spare_high_pin is not None:
        spare_high_pin.high()
    set_light_power(0)
    _setup_extints()

setup()
while True:
    loop()
