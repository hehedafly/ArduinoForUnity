# af_list.py — 引脚复用能力实测脚本（上板后经 REPL 运行）
#
# 用途：扫描 config.py 里用到的引脚，打印每根引脚支持的定时器通道（TimerN CHn），
#       并给出一版无冲突的 (timer, channel) 自动分配建议。
#
# 运行方式（REPL）：
#   >>> import af_list
#   >>> af_list.scan()
#
# 原理：
#   pyb.Pin(name).af_list() 返回 [PinAF, ...]，PinAF.name() 形如 'AF1_TIM2'，
#   直接从中提取定时器编号；通道号结合内置 PIN_TIMER_CH[(端口,引脚号)] 查得。
#
# 注意：不 import main（避免触发硬件初始化）。

import config
from pyb import Pin
import re

# ======================================================================
# pyboard v1.1 引脚名 -> (GPIO 端口字母, 引脚号) 静态映射（固定不变，不依赖固件字典）
# 以及 (端口, 引脚号) -> {timer_id: channel_id}（STM32F405/407 数据手册 Table 9）
# ======================================================================
PIN_TO_PORT = {
    "X1": ("A", 0), "X2": ("A", 1), "X3": ("A", 2), "X4": ("A", 3),
    "X5": ("A", 4), "X6": ("A", 5), "X7": ("A", 6), "X8": ("A", 7),
    "X9": ("B", 6), "X10": ("B", 7), "X11": ("B", 4), "X12": ("B", 5),
    "Y1": ("C", 6), "Y2": ("C", 7), "Y3": ("B", 8), "Y4": ("B", 9),
    "Y5": ("B", 12), "Y6": ("B", 13), "Y7": ("B", 14), "Y8": ("B", 15),
    "Y9": ("B", 10), "Y10": ("B", 11), "Y11": ("B", 0), "Y12": ("B", 1),
}

PIN_TIMER_CH = {
    ("A", 0):  {2: 1, 5: 1, 8: 1},     # X1  PA0
    ("A", 1):  {2: 2, 5: 2},           # X2  PA1
    ("A", 2):  {2: 3, 5: 3, 9: 1},     # X3  PA2
    ("A", 3):  {2: 4, 5: 4, 9: 2},     # X4  PA3
    ("A", 5):  {2: 1, 8: 1},           # X6  PA5
    ("A", 6):  {3: 1, 13: 1},          # X7  PA6
    ("A", 7):  {3: 2, 14: 1},          # X8  PA7
    ("B", 0):  {3: 3, 8: 2},           # Y11 PB0
    ("B", 4):  {3: 1},                 # X11 PB4
    ("B", 5):  {3: 2},                 # X12 PB5
    ("B", 6):  {4: 1},                 # X9  PB6
    ("B", 7):  {4: 2},                 # X10 PB7
    ("B", 11): {2: 4},                 # Y10 PB11
    ("B", 14): {12: 1},                # Y7  PB14
    ("B", 15): {12: 2},                # Y8  PB15
    ("C", 6):  {3: 1, 8: 1},           # Y1  PC6
    ("C", 7):  {3: 2, 8: 2},           # Y2  PC7
}

def _pin_port_num(pin_name):
    """从静态表查 (端口字母, 引脚号)。"""
    return PIN_TO_PORT.get(pin_name, (None, None))

def _af_name(af):
    try:
        return af.name()
    except Exception:
        return str(af)

def _scan_one(name):
    """打印该引脚支持的 TimerX CHn 列表。"""
    port, num = _pin_port_num(name)
    try:
        afs = Pin(name).af_list()
    except Exception as e:
        print("    !! 无法创建 Pin 或读 af_list: {0}".format(e))
        return

    # 从 'AFx_TIMy' 提取定时器编号，去重保序
    timers = []
    for af in afs:
        m = re.search(r'TIM(\d+)', _af_name(af))
        if m:
            t = int(m.group(1))
            if t not in timers:
                timers.append(t)

    ch_map = PIN_TIMER_CH.get((port, num), {})
    parts = []
    for t in timers:
        ch = ch_map.get(t)
        parts.append("Timer{0} CH{1}".format(t, ch) if ch is not None
                     else "Timer{0} (通道未映射)".format(t))

    if parts:
        print("    定时器通道: {0}".format(", ".join(parts)))
    else:
        names = [_af_name(af) for af in afs]
        print("    af_list: {0}".format(", ".join(names) if names else "(空)"))

def _avail_timers(name):
    """返回该引脚可用的 [(timer, channel), ...]（按 PIN_TIMER_CH + af_list 实测交集）。"""
    port, num = _pin_port_num(name)
    try:
        afs = Pin(name).af_list()
    except Exception:
        afs = []
    actual = []
    for af in afs:
        m = re.search(r'TIM(\d+)', _af_name(af))
        if m:
            actual.append(int(m.group(1)))
    ch_map = PIN_TIMER_CH.get((port, num), {})
    # 只返回"实测 af_list 里有"且"映射表有通道"的
    out = []
    for t in actual:
        ch = ch_map.get(t)
        if ch is not None:
            out.append((t, ch))
    return out

def scan():
    print("================ 给水泵引脚 (PUMP_PINS) ================")
    for i, name in enumerate(config.PUMP_PINS):
        print("[pump {0}] {1}".format(i, name))
        _scan_one(name)

    print("\n================ OG 引脚 ================")
    print("[OG] {0}".format(config.OG_PIN))
    _scan_one(config.OG_PIN)

    print("\n================ 光功率 DAC 引脚 ================")
    print("  光功率 DAC = DAC{0}（12-bit 0~3.3V，非定时器复用）".format(config.LIGHT_POWER_DAC))

    print("\n================ 舔水/传感器引脚 ================")
    for i, name in enumerate(config.LICK_PINS):
        print("[lick {0}] {1}".format(i, name))
        _scan_one(name)
    for nm in ["INFRARED_DETECT_PIN", "PRESS_LEVER_PIN", "SYNC_INPUT_PIN"]:
        name = getattr(config, nm)
        print("[{0}] {1}".format(nm, name))
        _scan_one(name)

    print("\n================ 自动分配建议 ================")
    _suggest_assignment()

def _suggest_assignment():
    used = set()
    pump_assign = []
    for i, name in enumerate(config.PUMP_PINS):
        assigned = None
        for (t, c) in _avail_timers(name):
            if (t, c) not in used:
                assigned = (t, c)
                used.add((t, c))
                break
        pump_assign.append((name, assigned))

    og_assigned = None
    for (t, c) in _avail_timers(config.OG_PIN):
        if (t, c) not in used:
            og_assigned = (t, c)
            used.add((t, c))
            break

    print("建议的 _PUMP_TIMER_CH（无冲突）：")
    for name, a in pump_assign:
        if a:
            print("    {0}: ({1}, {2})".format(name, a[0], a[1]))
        else:
            print("    {0}: 无可用定时器通道！需换引脚".format(name))
    print("建议的 _OG_TIMER_CH：")
    if og_assigned:
        print("    {0}: ({1}, {2})".format(config.OG_PIN, og_assigned[0], og_assigned[1]))
    else:
        print("    {0}: 无可用定时器通道！需换引脚".format(config.OG_PIN))
    print("\n>> 把上面 (timer, channel) 结果复制到 main.py 的 _PUMP_TIMER_CH / _OG_TIMER_CH 即可。")

if __name__ == "__main__":
    scan()
