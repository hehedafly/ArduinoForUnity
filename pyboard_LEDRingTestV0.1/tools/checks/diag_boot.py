# diag_boot.py — 软复位板子(Ctrl-D)并抓取完整启动输出，判定 main.py 是编译期还是运行期 OOM。
# 软复位会把 GC 堆重置为干净、无碎片状态，最接近真实上电。
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
    port = find_port(sys.argv[1] if len(sys.argv) > 1 and not sys.argv[1].startswith("-") else None)
    if "--port" in sys.argv:
        port = sys.argv[sys.argv.index("--port") + 1]
    if not port:
        print("NO PORT"); sys.exit(2)
    print("using port:", port)
    ser = serial.Serial(port, 115200, timeout=0.2)

    # 先排空当前缓冲
    time.sleep(0.3)
    if ser.in_waiting:
        ser.read(ser.in_waiting)

    # Ctrl-C 确保不在 paste/多行态，然后 Ctrl-D 软复位
    ser.write(b"\x03")
    time.sleep(0.3)
    ser.write(b"\x04")   # soft reboot -> 重跑 main.py

    # 抓 6 秒启动输出
    end = time.time() + 6.0
    buf = b""
    while time.time() < end:
        n = ser.in_waiting
        if n:
            buf += ser.read(n)
        else:
            time.sleep(0.05)

    print("===== BOOT OUTPUT (raw repr) =====")
    print(repr(buf))
    print("===== BOOT OUTPUT (decoded) =====")
    print(buf.decode("utf-8", "replace"))
    ser.close()


if __name__ == "__main__":
    main()
