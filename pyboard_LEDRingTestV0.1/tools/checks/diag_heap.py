# diag_heap.py — 连板子，先探活看清状态（崩溃循环 / REPL / 业务模式），再量启动堆。
# 用法: <hex-prism python> diag_heap.py [--port COM9]
import argparse
import sys
import time

import serial
from serial.tools import list_ports


def find_port(explicit):
    if explicit:
        return explicit
    for p in list_ports.comports():
        hw = (p.hwid or "").upper()
        if "F055" in hw or "9800" in hw:      # pyboard USB VCP VID:PID
            return p.device
    cs = [p.device for p in list_ports.comports()]
    return cs[0] if cs else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default=None)
    args = ap.parse_args()

    port = find_port(args.port)
    if not port:
        print("NO PORT FOUND")
        sys.exit(2)
    print("using port:", port)

    try:
        ser = serial.Serial(port, 115200, timeout=0.2)
    except Exception as e:
        print("OPEN FAIL (串口被占用？先关掉别的终端):", repr(e))
        sys.exit(3)

    def rd(t=0.8):
        time.sleep(t)
        n = ser.in_waiting
        return ser.read(n) if n else b""

    def show(tag, data):
        print("[" + tag + "] " + repr(data))

    # 1) 被动监听 2s：看板子是否在自发输出（崩溃循环会反复打横幅/错误）
    show("passive", rd(2.0))

    # 2) 活性探测：回车 + Ctrl-C + 一个必定回显的命令
    ser.write(b"\r\n")
    show("after CR", rd(0.5))
    ser.write(b"\x03")
    show("after CtrlC", rd(0.5))
    ser.write(b"print(12345)\r\n")
    probe = rd(0.8)
    show("probe print(12345)", probe)

    alive = b"12345" in probe
    print("REPL_ALIVE =", alive)
    if not alive:
        print("=> COM9 不是活 REPL：可能 main.py 正在跑（把输入当协议数据），"
              "或板子在崩溃重启循环，或 REPL 被 boot.py 挪到了 UART6。")
        # 再试一次：发业务握手关键字，看是否有业务帧回应
        ser.write(b"//check\r\n")
        show("probe //check", rd(1.0))
        ser.close()
        return

    def do(cmd, t=0.8):
        ser.write(cmd.encode() + b"\r\n")
        out = rd(t)
        print(">>> " + cmd)
        show("out", out)
        return out

    do("import gc")
    do("gc.collect()")
    do("print('FREE_BASE=', gc.mem_free())")
    do("import config")
    do("gc.collect()")
    do("print('FREE_AFTER_CONFIG=', gc.mem_free())")
    do("import comm")
    do("gc.collect()")
    do("print('FREE_AFTER_COMM=', gc.mem_free())")
    do("print('EXTINT_ROWS=', len(config.EXTINT_TABLE))")
    do("print('VERSION=', config.VERSION)")

    ser.close()


if __name__ == "__main__":
    main()
