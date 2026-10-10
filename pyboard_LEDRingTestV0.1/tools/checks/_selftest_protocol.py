# 离线自测：帧解析 round-trip + 混合流（不连硬件）
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

from unitysim import protocol as p

f = p.encode_frame("li", "-4:1")
s = p.encode_frame("cmd", "stv:0=0;1=0") + b"initialed:V2.5\r\n" + b"echo:2=1:echo\r\n"
msgs = p.FrameParser().feed(f + s, 0.5)
print(msgs)
assert msgs[0][:3] == ("frame", 0, "-4:1"), msgs[0]
assert msgs[0][3] == 0.5
assert msgs[1][0] == "frame" and msgs[1][1] == 7 and msgs[1][2].startswith("stv:")
assert msgs[2][:2] == ("line", "initialed:V2.5")
assert msgs[3][:2] == ("line", "echo:2=1:echo")

# 坏定界符重同步：DD 位置放错字节后紧跟正常帧
bad = bytes([0xAA, 0x00, 0x01, 0x30, 0x00]) + p.encode_frame("li", "1:1")
msgs2 = p.FrameParser().feed(bad, 1.0)
assert any(m[:3] == ("frame", 0, "1:1") for m in msgs2), msgs2
print("protocol selftest OK")
