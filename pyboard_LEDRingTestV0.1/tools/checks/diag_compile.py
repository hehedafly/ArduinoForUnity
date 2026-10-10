# diag_compile.py — 量 main/config/comm 的编译成本，判定 OOM 是编译期还是运行期。
# 只 compile() 不 exec()，安全；不 import main（避免跑进主循环）。
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
        if "F055" in hw or "9800" in hw:
            return p.device
    cs = [p.device for p in list_ports.comports()]
    return cs[0] if cs else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default=None)
    args = ap.parse_args()
    port = find_port(args.port)
    if not port:
        print("NO PORT"); sys.exit(2)
    print("using port:", port)
    try:
        ser = serial.Serial(port, 115200, timeout=0.2)
    except Exception as e:
        print("OPEN FAIL:", repr(e)); sys.exit(3)

    def rd(t=1.2):
        time.sleep(t)
        n = ser.in_waiting
        return ser.read(n) if n else b""

    def do(cmd, t=1.2):
        ser.write(cmd.encode() + b"\r\n")
        out = rd(t)
        print(">>> " + cmd)
        print("    " + repr(out))
        return out

    ser.write(b"\x03"); time.sleep(0.3); ser.write(b"\r\n"); rd(0.5)

    # 逐个文件：读源码 -> compile -> 报告 src 长度、编译后剩余、是否 OOM
    prog = (
        "import gc\n"
        "def probe(fn):\n"
        "    gc.collect(); a=gc.mem_free()\n"
        "    try:\n"
        "        s=open(fn).read(); n=len(s)\n"
        "        gc.collect(); b=gc.mem_free()\n"
        "        c=compile(s,fn,'exec')\n"
        "        gc.collect(); d=gc.mem_free()\n"
        "        del s,c\n"
        "        gc.collect(); e=gc.mem_free()\n"
        "        print(fn,'src',n,'free0',a,'freeSrc',b,'freeCode',d,'codeSize~',b-d,'freed',e)\n"
        "    except MemoryError as ex:\n"
        "        print(fn,'COMPILE_OOM',repr(ex),'free',gc.mem_free())\n"
        "    except OSError as ex:\n"
        "        print(fn,'NOFILE',repr(ex))\n"
    )
    # 用 exec 定义 probe（多行函数走 REPL  paste 模式不稳，改用单行 exec 字符串）
    do("import gc")
    do("gc.collect(); print('FREE_NOW', gc.mem_free())")
    for fn in ("main.py", "config.py", "comm.py"):
        do("s=open('%s').read(); print('%s SRCLEN', len(s))" % (fn, fn), 1.0)
        do("gc.collect(); print('before', gc.mem_free())")
        do("c=compile(s,'%s','exec'); gc.collect(); print('AFTER_COMPILE', gc.mem_free())" % fn, 2.0)
        do("del s,c; gc.collect(); print('after_del', gc.mem_free())")

    print("--- prog (unused) ---")
    _ = prog
    ser.close()


if __name__ == "__main__":
    main()
