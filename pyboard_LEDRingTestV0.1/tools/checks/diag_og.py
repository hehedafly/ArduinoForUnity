# diag_og.py — 探测板子状态并安全验证 ogset 是否被处理。
# 只发 statusValue(读快照) 和 cmd:ogset;0;0(关断，X1 默认即低，无副作用)，不发拉高命令。
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

import serial
from serial.tools import list_ports
from unitysim import protocol


def find_port(explicit):
    if explicit:
        return explicit
    for p in list_ports.comports():
        hw = (p.hwid or "").upper()
        if "F055" in hw or "9800" in hw:
            return p.device
    cs = [p.device for p in list_ports.comports()]
    return cs[0] if cs else None


def main():
    port = find_port(sys.argv[sys.argv.index("--port") + 1] if "--port" in sys.argv else None)
    if not port:
        print("NO PORT"); sys.exit(2)
    print("port:", port)
    try:
        ser = serial.Serial(port, 115200, timeout=0.2)
    except Exception as e:
        print("OPEN FAIL(串口被占用？):", repr(e)); sys.exit(3)

    def rd(t=1.2):
        time.sleep(t)
        n = ser.in_waiting
        return ser.read(n) if n else b""

    print("[passive]", repr(rd(1.5)))

    # 1) 业务模式探测：发 statusValue 帧，看是否回 stv:
    ser.write(protocol.encode_frame("cmd", "statusValue"))
    r = rd(1.5)
    print("[statusValue]", repr(r))

    if b"stv:" in r:
        print("=> BUSINESS MODE: main.py 正在运行")
        # 2) 安全验证 ogset 分发：cmd:ogset;0;0 = 关断（X1 默认即低，无硬件副作用）
        ser.write(protocol.encode_frame("cmd", "cmd:ogset;0;0"))
        r2 = rd(1.2)
        print("[ogset;0;0]", repr(r2))
        print("=> ogset 被处理(echo 回来) =", b"echo:cmd:ogset;0;0:echo" in r2)
        # 3) 对照 ogpwm 关断（同样安全）
        ser.write(protocol.encode_frame("cmd", "cmd:ogpwm;0;20;50;0"))
        r3 = rd(1.2)
        print("[ogpwm;...;0]", repr(r3))
    else:
        # 不是业务模式 -> 探 REPL
        ser.write(b"\x03"); time.sleep(0.3); rd(0.3)
        ser.write(b"print(12345)\r\n")
        rp = rd(1.0)
        print("[repl probe]", repr(rp))
        if b"12345" in rp:
            print("=> REPL MODE: main.py 没在跑（极可能仍编译期 OOM 或未部署成功）")
            ser.write(b"import gc;gc.collect();print('FREE',gc.mem_free())\r\n")
            print("[mem]", repr(rd(1.0)))
            ser.write(b"import os;print([f for f in os.listdir()])\r\n")
            print("[files]", repr(rd(1.0)))
        else:
            print("=> 既非业务也非交互 REPL，板子状态异常")

    ser.close()


if __name__ == "__main__":
    main()
