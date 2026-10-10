# checks/check_handshake.py — 握手完整检测（引用 unitysim，不含模拟逻辑）
# 用法: <hex-prism python> check_handshake.py --port COM7
import argparse
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

from unitysim import BoardLink, UnitySimulator

FAILS = []


def check(ok, name, detail=""):
    print("[{0}] {1}{2}".format("PASS" if ok else "FAIL", name, ("  | " + detail) if detail else ""))
    if not ok:
        FAILS.append(name)


def main():
    ap = argparse.ArgumentParser(description="pyboard 握手检测（复刻 Unity CreateSerialConnection）")
    ap.add_argument("--port", required=True)
    ap.add_argument("--baud", type=int, default=115200)
    args = ap.parse_args()

    link = BoardLink(args.port, args.baud)
    try:
        sim = UnitySimulator(link)
        ok = sim.connect()
        check(ok, "handshake #1: initialed -> ACK -> ACK_OK（开机 banner 可能已错过，不作严格断言）",
              "banners seen={0}".format(sim.banners))
        check(sim.version in ("V2.4", "V2.5"), "version 在兼容列表内", "got {0}".format(sim.version))

        # 重连：板已在主循环，//forceinit 应触发重握手并再次先发两遍 banner（此时严格断言）
        ok2 = sim.connect()
        check(ok2, "handshake #2: forceinit 重握手成功")
        check(sim.banners == 2, "重握手 banner 恰好两遍", "got {0}".format(sim.banners))
        check(sim.device_type == "Pyboard", "板型识别为 Pyboard", "got {0}".format(sim.device_type))
    finally:
        link.close()

    print("\n{0}".format("ALL PASS" if not FAILS else "FAILED: {0}".format(FAILS)))
    sys.exit(0 if not FAILS else 1)


if __name__ == "__main__":
    main()
