# deploy_test.py — 通过 REPL(CDC-only 板无 MSC 盘) 用 hex 分块部署文件，并软复位验证启动。
# 逐行等待 '>>> ' 提示符（比固定延时稳），预导入 ubinascii/os，ASCII 化内容（与 deploy5 一致）。
# 用法: <hex-prism python> deploy_test.py [--port COM9] [file:remote ...]
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
    args = sys.argv[1:]
    port = None
    if "--port" in args:
        i = args.index("--port"); port = args[i + 1]; del args[i:i + 2]
    port = find_port(port)
    if not port:
        print("NO PORT"); sys.exit(2)

    files = []
    for a in args:
        if ":" in a:
            loc, rem = a.split(":", 1)
        else:
            loc = rem = a
        files.append((loc, rem))
    if not files:
        files = [("hw.py", "hw.py"), ("main.py", "main.py")]

    print("port:", port, "files:", files)
    ser = serial.Serial(port, 115200, timeout=0.2)

    def drain(t=0.05):
        time.sleep(t)
        n = ser.in_waiting
        return ser.read(n) if n else b""

    def send_wait(line, timeout=3.0):
        ser.write(line.encode() + b"\r")
        end = time.time() + timeout
        buf = b""
        while time.time() < end:
            n = ser.in_waiting
            if n:
                buf += ser.read(n)
                if buf.rstrip().endswith(b">>>"):
                    return buf
            else:
                time.sleep(0.008)
        return buf

    # 拿到干净提示符 + 预导入依赖
    ser.write(b"\x03"); time.sleep(0.3); drain(0.1)
    send_wait(""); drain(0.1)
    r = send_wait("import ubinascii, os")
    if b"Traceback" in r:
        print("IMPORT DEPS FAIL", r); ser.close(); sys.exit(4)

    def deploy(local, remote):
        data = open(local, "rb").read()
        data = data.decode("utf-8", "replace").encode("ascii", "replace")  # 非 ASCII -> '?'
        MAXB = 2000; CH = 200
        off = 0; part = 0
        while off < len(data):
            hx = data[off:off + MAXB].hex()
            mode = "wb" if part == 0 else "ab"
            send_wait("_hp=''")
            for i in range(0, len(hx), CH):
                send_wait("_hp+='" + hx[i:i + CH] + "'")
            r = send_wait("f=open('%s','%s');f.write(ubinascii.unhexlify(_hp));f.close()" % (remote, mode))
            if b"Traceback" in r or b"Error" in r:
                print("WRITE ERR part", part, r); ser.close(); sys.exit(5)
            off += MAXB; part += 1
        r = send_wait("print('SZ_%s',os.stat('%s')[6])" % (remote, remote))
        print("deployed", local, "->", remote, "| ascii bytes", len(data), "|", r.decode("utf-8", "replace").strip())

    for loc, rem in files:
        deploy(loc, rem)

    # 软复位并抓启动输出，验证是否还 OOM
    drain(0.2)
    ser.write(b"\x04")   # Ctrl-D soft reboot -> 重跑 main.py
    end = time.time() + 8.0
    buf = b""
    while time.time() < end:
        n = ser.in_waiting
        if n:
            buf += ser.read(n)
        else:
            time.sleep(0.05)
    print("===== BOOT OUTPUT =====")
    print(buf.decode("utf-8", "replace"))
    ok = (b"MemoryError" not in buf)
    print("BOOT_OK(no MemoryError) =", ok)
    ser.close()


if __name__ == "__main__":
    main()
