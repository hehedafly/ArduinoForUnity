# unitysim.protocol — 帧协议编解码（与 pyboard comm.py / Unity CommandConverter 对齐）
#
# 帧: 0xAA + typeId(1B) + len(1B) + content + 0xDD + \r\n
# 明文行: echo / initialed / ACK_OK / banner 等，以 \r\n 结尾
# 文本命令(Unity -> 板): "//" + cmd + "\n"（板端双 0x2F 后读到换行/空闲为止）

SERIAL_PRINT_TYPE = [
    "li",        # 0
    "en",        # 1
    "pr",        # 2
    "ci",        # 3
    "log",       # 4
    "echo",      # 5
    "vc",        # 6
    "cmd",       # 7
    "debugLog",  # 8
    "st",        # 9
    "si",        # 10
    "ms",        # 11
]
_TYPE_ID = {name: i for i, name in enumerate(SERIAL_PRINT_TYPE)}

FRAME_START = 0xAA
FRAME_END = 0xDD

BOARD_BANNER = "pyboardV1.1Plus"   # 与 pyboard config.BOARD_BANNER 一致


def encode_frame(type_str, content):
    """content 为 str；返回完整帧字节（含尾部 \r\n，与板端 encode_frame 一致）。"""
    tid = _TYPE_ID[type_str]
    body = content.encode("ascii", "replace")[:0xFF]
    return bytes([FRAME_START, tid, len(body)]) + body + bytes([FRAME_END, 0x0D, 0x0A])


def text_cmd(cmd):
    """Unity WriteLine("//xxx") 形式：双斜杠前缀 + 换行结尾。"""
    return b"//" + cmd.encode("ascii") + b"\n"


def plain_line(s):
    return s.encode("ascii") + b"\n"


class FrameParser:
    """增量解析器：feed(bytes) -> [msg, ...]，msg = ("line", str) 或 ("frame", tid, content)。"""

    _OUT, _TID, _LEN, _CONTENT, _END = range(5)

    def __init__(self):
        self._state = self._OUT
        self._tid = 0
        self._need = 0
        self._buf = bytearray()
        self._line = ""

    def feed(self, data, t=None):
        """t 为该数据块的到达时间戳（link 层传入）；msg 末位携带它，供时序断言用到达时刻而非分派时刻。"""
        out = []
        for b in data:
            self._step(b, out, t)
        return out

    def _step(self, b, out, t):
        st = self._state
        if st == self._OUT:
            if b == FRAME_START:
                self._state = self._TID
            elif b in (0x0D, 0x0A):
                if self._line:
                    out.append(("line", self._line, t))
                    self._line = ""
            else:
                self._line += chr(b)
        elif st == self._TID:
            self._tid = b
            self._state = self._LEN
        elif st == self._LEN:
            self._need = b
            self._buf = bytearray()
            self._state = self._CONTENT if self._need > 0 else self._END
        elif st == self._CONTENT:
            self._buf.append(b)
            if len(self._buf) >= self._need:
                self._state = self._END
        else:  # _END
            if b == FRAME_END:
                out.append(("frame", self._tid, self._buf.decode("ascii", "replace"), t))
            else:
                # 定界符不符：丢弃本帧，按 OUT 状态重处理该字节
                self._state = self._OUT
                self._step(b, out, t)
                return
            self._state = self._OUT
