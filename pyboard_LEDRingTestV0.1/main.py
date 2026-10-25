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
import micropython
# 禁用中断字符（Ctrl-C/0x03）：STM32 的 USB VCP 接收路径对每个入站字节做中断字符检查，
# 上位机帧里作为普通数据出现的 0x03（如 "6=0"/"7=0" 等 3 字节 content 的长度字节）
# 会异步触发 KeyboardInterrupt，打穿 main.py 并引发软复位（Unity 每次退出稳定触发）。
# 禁用后 0x03 回归普通数据；维护中断改用 exitmain 命令干净退出主循环。
micropython.kbd_intr(-1)
from pyb import Pin, ExtInt, DAC, Timer
from utime import ticks_us, sleep_ms, ticks_ms, ticks_diff
import gc
# 堆调优：当已分配堆增长到「空闲的 1/4」时自动触发 GC，抑制 SendBuffer 反复重建
# bytearray 造成的碎片化——这是“分配 109 字节都失败”这类 OOM 的主因（此前全程未做 GC 调优）。
gc.threshold(gc.mem_free() // 4 + gc.mem_alloc())

# ======================================================================
# 一、硬件驱动（GPIO / ExtInt / DAC / 定时器 one-pulse）
# ======================================================================

# ---- 硬件层已拆到 hw.py（减小 main.py 编译峰值，避免板载编译 OOM）----
from hw import pump, og, ms, pump_pins, led_pin, spare_high_pin, set_light_power, tp_reset_pin

# ---- 传感器中断标志 ----
lick_flags = [0] * 8
lick_ticks = [0] * 8
lick_count = [0] * 8
_lick_dir = [True] * 8        # True=REACH(RISING), False=LEAVE(FALLING)
infrared_flags = [0, 0]
press_flag = [0]
sync_flag = [0]

# ---- 中断 token → pyb 常量（对标 Arduino attachInterrupt 的 mode / 上下拉）----
_EDGE = {
    "rising_falling": ExtInt.IRQ_RISING_FALLING,
    "rising": ExtInt.IRQ_RISING,
    "falling": ExtInt.IRQ_FALLING,
    "change": ExtInt.IRQ_RISING_FALLING,   # STM32 无独立 CHANGE，双沿等价
}
_PULL = {"up": Pin.PULL_UP, "down": Pin.PULL_DOWN, "none": Pin.PULL_NONE}

# ---- 处理器工厂：签名 (pin_name, param) -> callback；只置标志，组帧仍交给 _poll_sensors ----
def _h_lick(pin, i):
    """舔水双沿：读电平 高=REACH(计数) / 低=LEAVE。"""
    p_in = Pin(pin, Pin.IN)
    def cb(line):
        reach = p_in.value() == 1
        if reach:
            lick_count[i] += 1          # 只有 REACH 计数，与 lick_count[i]++ 语义一致
        lick_flags[i] = 1
        lick_ticks[i] = ticks_us()
        _lick_dir[i] = reach
    return cb

def _h_infrared(pin, i):
    """红外门控双沿：高=In / 低=leave（置 infrared_flags[0]/[1]）。"""
    p_in = Pin(pin, Pin.IN)
    def cb(line):
        if p_in.value() == 1:
            infrared_flags[0] = 1
        else:
            infrared_flags[1] = 1
    return cb

def _h_press(pin, i):
    def cb(line):
        press_flag[0] = 1
    return cb

def _h_sync(pin, i):
    def cb(line):
        sync_flag[0] = 1
    return cb

INT_HANDLERS = {"lick": _h_lick, "infrared": _h_infrared, "press": _h_press, "sync": _h_sync}

_extints = []

def _setup_extints():
    """照 config.EXTINT_TABLE 注册中断（对标 Arduino attachInterrupt 清单）。
    改「哪个引脚挂什么中断」只需编辑 config.py 的表，本函数无需改动。"""
    for pin, edge, pull, key, param in config.EXTINT_TABLE:
        cb = INT_HANDLERS[key](pin, param)
        _extints.append(ExtInt(pin, _EDGE[edge], _PULL[pull], cb))

# ---- 软件单脉冲（共享定时器回调，对标 Arduino DueTimer 单一 pumpTimer） ----
# 给水输出"单次脉冲"：置高引脚 + 共享定时器在 micros 微秒后回调拉低（OG 已改硬件 PWM，见下）。
# 不是周期 PWM 波形；对引脚无定时器复用约束，故给水泵可在 PE0~PE7 完全连续排列。
# 精度实测：回调下降沿约 +30~40µs 中断延迟（给水 10ms 场景足够；OG 已改硬件 PWM，不受此限）。

# ---- Pump / OG / MS 类与实例已移至 hw.py（见顶部 from hw import …）----

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
    "touchpannelReset": 0,
}
water_flush = [0] * 8
_exit_main = False   # exitmain 后门标志：置位后主循环干净退出，落回 UART6 REPL
_tp_reset_until = 0  # 触摸面板复位：0=空闲；否则为 ticks_ms 到期时刻（软件计时，见 _poll_tp_reset）

def _set_exit_main():
    global _exit_main
    _exit_main = True
    comm.send_plain_line("echo:exitmain:echo")

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
    # 对标 Arduino V2_5 init_by_PC：INDEBUGMODE/autoStressTest 仅在 reset_connect_status=True
    # （forceinit）时清零；clear（False）时保留，避免软复位/清状态丢掉调试与压测开关。
    state.update({
        "waiting": 1, "lick_mode": 0, "trial": 0, "trial_set": 0,
        "now_pos": -1, "pre_pos": -1, "lick_rec_pos": -1,
        "tempTrialStatusMark": -1, "waterServeWhenLick": 0,
        "waterServeManual": -1, "OGActiveMills": 100, "miniscopeRecord": 0,
        "tempLickPos": -1, "lickEndThresholdMills": -1,
        "touchpannelReset": 0,
    })
    for i in range(8):
        water_flush[i] = 0
        lick_count[i] = 0
    pump.off_all()
    og.off()
    ms.off_all()
    set_light_power(0)
    _tp_reset_release()   # 复位线立即回高，绝不留在低电平
    comm.send_buffer.clear()
    if reset_connect_status:
        state["INDEBUGMODE"] = 0
        state["autoStressTest"] = 0
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

def print_status_value():
    """stv 状态快照打包（交接约定见上位机侧文档，本工程不含该文件）：不含 9=waterServeManual，防重连后意外出水。"""
    s = state
    parts = [
        "0={0}".format(s["lick_mode"]),    "1={0}".format(s["trial"]),
        "2={0}".format(s["trial_set"]),    "3={0}".format(s["now_pos"]),
        "4={0}".format(s["lick_rec_pos"]), "5={0}".format(s["INDEBUGMODE"]),
        "6={0}".format(s["OGActiveMills"]),"7={0}".format(s["miniscopeRecord"]),
        "8={0}".format(s["waterServeWhenLick"]), "10={0}".format(s["lightControl"]),
    ]
    parts.append("a0=" + ",".join(str(x) for x in config.WATER_SERVE_MICROS))
    parts.append("a1=" + ",".join(str(x) for x in lick_count))
    parts.append("a2=" + ",".join(str(x) for x in water_flush))
    body = "stv:" + ";".join(parts)
    if len(body) > 250:   # 帧内容上限 0xFF，留余量
        comm.push_logical("log:statusValue too long, skipped")
        return
    comm.push_logical("cmd:" + body)

def parse_status_value(body):
    """stv 快照重放：跳过 trial_set(2)/waterServeManual(9)；water_flush 直接置位、不走翻转语义；
    OG/miniscope 只恢复状态值、不动引脚（引脚状态由后续命令决定）。"""
    for seg in body.split(";"):
        if not seg or "=" not in seg:
            continue
        sid, _, sval = seg.partition("=")
        if sid.startswith("a"):
            try:
                vals = [int(x) for x in sval.split(",") if x != ""]
            except ValueError:
                continue
            if sid == "a0":
                for i, v in enumerate(vals[:8]):
                    config.WATER_SERVE_MICROS[i] = v
            elif sid == "a1":
                for i, v in enumerate(vals[:8]):
                    lick_count[i] = v
            elif sid == "a2":
                for i, v in enumerate(vals[:8]):
                    water_flush[i] = 1 if v > 0 else 0
                    pump_pins[i].high() if water_flush[i] else pump_pins[i].low()
            continue
        try:
            sid_i = int(sid)
            val = int(sval)
        except ValueError:
            continue
        if sid_i in (2, 9) or not (0 <= sid_i < len(config.STATE_ORDER)):
            continue
        var_name = config.STATE_ORDER[sid_i]
        if var_name == "now_pos":
            state["pre_pos"] = state["now_pos"]
        state[var_name] = val
        if var_name == "lightControl":
            v = 4096 if val > 4096 else (0 if val < 0 else val)
            state["lightControl"] = v
            set_light_power(v)   # 旧渠道：两路 DAC 同值（需分控用 cmd:lpset）

# ======================================================================
# 三、命令解析（对标 commandParse + 新文法分发）
# ======================================================================

# ---- 新文法：<type>:<case>;<p1>;<p2>...（冒号=类型，分号=参数隔离） ----
def _opt_int(parts, i, default):
    """取第 i 个参数：缺失/空串→default；存在但非数值→None（调用方整条丢弃）。"""
    if i >= len(parts) or parts[i] == "":
        return default
    try:
        return int(parts[i])
    except ValueError:
        return None

def _cmd_ogpwm(args):
    """cmd:ogpwm;<ch>;<freq>;<duty>;<time>：修改调制参数并按 time 应用到 PWM 线；ch=-1 同参作用所有通道。"""
    ch = _opt_int(args, 0, None)
    if ch not in (0, 1, -1):
        return False
    freq = _opt_int(args, 1, config.OG_PWM_DEFAULT_FREQ)
    duty = _opt_int(args, 2, config.OG_PWM_DEFAULT_DUTY)
    t = _opt_int(args, 3, 0)
    if freq is None or duty is None or t is None:
        return False
    freq = min(max(freq, config.OG_PWM_FREQ_MIN), config.OG_PWM_FREQ_MAX)
    duty = min(max(duty, config.OG_PWM_DUTY_MIN), config.OG_PWM_DUTY_MAX)
    if t < -1:
        t = 0
    if t == 0:
        og.pwm_off(ch)
    elif t < 0:
        og.pwm_mod(ch, freq, duty)
    else:
        og.pwm_timed(ch, freq, duty, t)
    return True

def _cmd_ogset(args):
    """cmd:ogset;<ch>;<time>：门控线开/关（独立于 PWM，不改调制参数）；ch=-1 同参作用所有通道。"""
    ch = _opt_int(args, 0, None)
    if ch not in (0, 1, -1):
        return False
    t = _opt_int(args, 1, 0)
    if t is None:
        return False
    if t < -1:
        t = 0
    if t == 0:
        og.gate_set(ch, False)
    elif t < 0:
        og.gate_set(ch, True)
    else:
        og.gate_timed(ch, t)
    return True

def _cmd_msset(args):
    """cmd:msset;<ch>;<state>：miniscope 记录开/关（无定时）；ch=-1 同参作用所有通道。"""
    ch = _opt_int(args, 0, None)
    if ch != -1 and not (0 <= ch < len(config.MS_RECORD_PINS)):
        return False
    st = _opt_int(args, 1, None)
    if st is None:
        return False
    ms.set(ch, st != 0)
    return True

def _cmd_lpset(args):
    """cmd:lpset;<ch>;<value>：光功率 DAC 2 路独立设置（0~4095，12-bit）；ch=-1 同值作用所有通道。"""
    ch = _opt_int(args, 0, None)
    if ch != -1 and not (0 <= ch < len(config.LIGHT_POWER_DACS)):
        return False
    v = _opt_int(args, 1, None)
    if v is None:
        return False
    set_light_power(v, ch)
    return True

def _parse_new_cmd(body, raw):
    """命令体按 ; split：第一个值为 case（进 switch），其余为顺序参数。"""
    parts = body.split(";")
    case = parts[0]
    args = parts[1:]
    if case == "ogpwm":
        ok = _cmd_ogpwm(args)
    elif case == "ogset":
        ok = _cmd_ogset(args)
    elif case == "msset":
        ok = _cmd_msset(args)
    elif case == "lpset":
        ok = _cmd_lpset(args)
    else:
        return   # 其它 case 预留，静默忽略
    if ok:
        comm.send_plain_line("echo:{0}:echo".format(raw))
        if state["INDEBUGMODE"] > 0:
            print_status()

def command_parse(cmd):
    cmd = cmd.replace("\n", "").replace("\r", "")
    s = state

    # 新文法分发：首冒号前为类型（当前仅 cmd），与旧 Arduino 渠道并存（旧命令串不含冒号）
    if ":" in cmd:
        ctype, _, body = cmd.partition(":")
        if ctype == "cmd":
            _parse_new_cmd(body, cmd)
        elif ctype == "stv":
            parse_status_value(body)   # 断线重连后 C# 原样回发的快照体
        # 其它类型预留，静默忽略
        return

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
    if cmd == "exitmain":
        _set_exit_main(); return
    if cmd == "statusValue":
        print_status_value(); return

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
                    og.legacy_set(val)   # 旧渠道：通道 0 的 PWM 线全高语义，不改调制参数
                elif var_name == "miniscopeRecord":
                    ms.set(0, val != 0)   # 旧渠道：映射到通道 0
                elif var_name == "now_pos":
                    s["pre_pos"] = s["now_pos"]
                elif var_name == "lightControl":
                    v = 4096 if val > 4096 else (0 if val < 0 else val)
                    s["lightControl"] = v
                    set_light_power(v)   # 旧渠道：两路 DAC 同值（需分控用 cmd:lpset）
                elif var_name == "touchpannelReset":
                    touchpannel_reset(val)   # >0=毫秒低脉冲后回高；pin=None 或 <=0 仅记状态
            return

# ======================================================================
# 四、主循环（对标 loop）
# ======================================================================

# ---- 触摸面板复位（软件计时，不阻塞主循环；pin=None 时全程不碰引脚）----
def _tp_reset_release():
    global _tp_reset_until
    _tp_reset_until = 0
    if tp_reset_pin is not None:
        tp_reset_pin.high()

def touchpannel_reset(mills):
    """>0：拉低 mills 毫秒后回高（主循环软件计时）；pin=None 或 <=0 则不动引脚。"""
    global _tp_reset_until
    if tp_reset_pin is None or mills <= 0:
        return False
    tp_reset_pin.low()
    _tp_reset_until = ticks_ms() + mills
    return True

def _poll_tp_reset():
    global _tp_reset_until
    if _tp_reset_until and ticks_diff(ticks_ms(), _tp_reset_until) >= 0:
        _tp_reset_release()

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
                # 对标 Arduino V2_5：TrialStart 后 500ms，连发 3 次 li:-4:1，间隔 50ms；
                # 每次 push 后立即冲刷，保证间隔真实（位置模式需第 2 个 -4 才触发 EndTrial）
                sleep_ms(500)
                for _ in range(3):
                    comm.push_logical("{0}:-4:1".format(config.SERIAL_PRINT_TYPE[0]))
                    _flush_send()
                    sleep_ms(50)
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

def _flush_send():
    """立即冲刷发送队列（压测等需即时发送的路径用）；loop 内仍走分片限流冲刷。"""
    while not comm.send_buffer.is_empty():
        chunk = comm.send_buffer.pop_chunk(512)
        if chunk:
            comm.send_bytes(chunk)

def loop():
    if not comm.handshake_done:
        comm.handshake_loop()

    _poll_sensors()
    _poll_trial_status()
    _poll_water_manual()
    _poll_tp_reset()

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
    ms.off_all()
    led_pin.low()
    if spare_high_pin is not None:
        spare_high_pin.high()
    if tp_reset_pin is not None:
        tp_reset_pin.high()   # 触摸面板复位线：空闲态=高（低有效）
    set_light_power(0)
    _setup_extints()

setup()
gc.collect()   # 回收编译/初始化的一次性垃圾，给主循环留出最大空闲堆
while True:
    loop()
    if _exit_main:
        break
# 干净退出主循环（exitmain 后门）：恢复 Ctrl-C 并落回 UART6 REPL，供维护/deploy 接管
micropython.kbd_intr(3)
