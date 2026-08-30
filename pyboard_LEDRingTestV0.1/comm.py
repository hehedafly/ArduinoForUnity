# comm.py — 通讯层：帧编解码 + USB VCP/UART 收发 + 握手 + 接收状态机 + 发送缓冲
#
# 本文档承载"通讯"职责，所有与协议、串口、缓冲相关的内容都在这里。
# 帧协议（与 Unity 上位机严格一致）：
#   发送帧: [0xAA][typeId(1B)][len(1B)][content...][0xDD]  总长 = 4 + len
#   接收帧: [typeId(1B)][len(1B)][content...]（已剥离 0xAA/0xDD）
#   文本命令: 两个 0x2F '/' 之间夹一行文本（如 /check/）

import config
from utime import ticks_ms, ticks_diff, sleep_ms

# ==================== typeId 映射（下标即 typeId，与 config.SERIAL_PRINT_TYPE 一致） ====================
_TYPE_ID = {name: i for i, name in enumerate(config.SERIAL_PRINT_TYPE)}

# ==================== 帧编解码 ====================
def encode_frame(type_str, content):
    """把 '<type>' + '<content>' 编码为字节流帧。
    content 是字符串，长度截断到 0xFF。
    对标原 serial_send -> serial_sendRaw：帧后跟一个 \\r\\n（println）。
    返回 bytes：0xAA + typeId + len + content + 0xDD + \\r\\n。
    """
    tid = _TYPE_ID[type_str]
    body = content.encode("ascii")[:0xFF]
    return bytes([config.FRAME_START, tid, len(body)]) + body + bytes([config.FRAME_END, 0x0D, 0x0A])

def decode_frame_content(data):
    """解析接收帧的 content 字段。data 为 [typeId][len][content...]。
    返回 content 字符串，异常返回 None。
    """
    if len(data) < 2:
        return None
    tid = data[0]
    n = data[1]
    if len(data) != 2 + n:
        return None
    try:
        return bytes(data[2:2 + n]).decode("ascii")
    except Exception:
        return None

# ==================== 发送缓冲 ====================
class SendBuffer:
    """简单字节队列，主循环单线程访问，无需锁。"""
    def __init__(self, max_bytes=4096):
        self._data = bytearray()
        self._max = max_bytes

    def push(self, frame):
        self._data.extend(frame)
        if len(self._data) > self._max:
            # MicroPython 的 bytearray 不支持 del b[:n]，用切片重赋值
            self._data = self._data[len(self._data) - self._max:]

    def pop_chunk(self, n):
        if not self._data:
            return b""
        chunk = bytes(self._data[:n])
        self._data = self._data[n:]
        return chunk

    def is_empty(self):
        return len(self._data) == 0

    def clear(self):
        self._data = bytearray()

    def size(self):
        return len(self._data)

send_buffer = SendBuffer()

def push_logical(logical_str):
    """把 '<type>:<content>' 逻辑字符串编码成帧入队。"""
    if ":" not in logical_str:
        return False
    type_str, content = logical_str.split(":", 1)
    if type_str not in _TYPE_ID:
        return False
    send_buffer.push(encode_frame(type_str, content))
    return True

# ==================== 通讯对象 ====================
_serial = None

def open_serial():
    global _serial
    if config.USE_USB_SERIAL:
        from pyb import USB_VCP
        _serial = USB_VCP()
    else:
        from pyb import UART
        _serial = UART(config.UART_ID, config.UART_BAUD)
    return _serial

# ==================== 握手 ====================
handshake_done = False

def set_handshake(done):
    global handshake_done
    handshake_done = done

def _send_line(s):
    if config.USE_USB_SERIAL:
        _serial.write(s.encode() + b"\r\n")
    else:
        _serial.write(s + "\r\n")

def send_plain_line(s):
    """发送一行明文（+\\r\\n），对标原 CUSTOM_SERIAL.println()。
    用于 echo / 握手 / 错误信息等非帧输出。"""
    _send_line(s)

def handshake_loop():
    global handshake_done
    handshake_done = False
    cmd = ""
    last_send = ticks_ms() - 1000
    while not handshake_done:
        if ticks_diff(ticks_ms(), last_send) >= 500:
            _send_line("initialed:" + config.VERSION)
            last_send = ticks_ms()
        while _serial.any() > 0 and not handshake_done:
            c = _read_char()
            if c is None:
                break
            if c in ("\n", "\r"):
                if cmd.endswith("ACK"):
                    handshake_done = True
                    _send_line("ACK_OK")
                cmd = ""
            else:
                cmd += c
        sleep_ms(10)

# ==================== 底层收发 ====================
def send_bytes(data):
    _serial.write(data)

def _read_char():
    c = _serial.read(1)
    if c is None:
        return None
    if isinstance(c, bytes):
        return chr(c[0]) if c else None
    return c

# ==================== 接收状态机 ====================
_rec = {
    "recording": False,
    "data": bytearray(),
    "plain_mark": False,
}

def reset_recv_state():
    _rec["recording"] = False
    _rec["data"] = bytearray()
    _rec["plain_mark"] = False

def process_read(on_command):
    """处理所有可用输入字节。on_command(cmd_str) 为回调，收到完整命令时调用。"""
    while _serial.any() > 0:
        b = _serial.read(1)
        if b is None:
            break
        inbyte = b[0] if isinstance(b, bytes) else ord(b)

        if not _rec["recording"] and inbyte == config.FRAME_START:      # 0xAA
            _rec["recording"] = True
            _rec["data"] = bytearray()
        elif not _rec["recording"]:
            if inbyte == config.TEXT_CMD_MARK:                           # 0x2F '/'
                if not _rec["plain_mark"]:
                    _rec["plain_mark"] = True
                else:
                    line = _read_line()
                    on_command(line)
                    _rec["plain_mark"] = False
                    break
        else:
            if inbyte == config.FRAME_END:                               # 0xDD
                content = decode_frame_content(_rec["data"])
                if content is not None:
                    on_command(content)
                _rec["recording"] = False
                _rec["data"] = bytearray()
                break
            elif inbyte == config.FRAME_START:                           # 0xAA 重入
                _rec["recording"] = True
                _rec["data"] = bytearray()
            else:
                _rec["data"].append(inbyte)
                if len(_rec["data"]) >= 512:
                    _rec["data"] = bytearray()
                    _rec["recording"] = False

def _read_line():
    """读一行文本（到换行/回车或超时）。"""
    line = ""
    start = ticks_ms()
    while ticks_diff(ticks_ms(), start) < 100:
        while _serial.any() > 0:
            c = _read_char()
            if c in ("\n", "\r"):
                return line
            line += c
        sleep_ms(1)
    return line
