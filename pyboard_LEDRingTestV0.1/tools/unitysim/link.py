# unitysim.link — pyserial 串口封装（仅 I/O，不含业务逻辑）
import time

import serial

from . import protocol


def mono():
    return time.monotonic()


class BoardLink:
    """板子串口的纯 I/O 封装：发送帧/明文，增量解析接收流。"""

    def __init__(self, port, baud=115200):
        self.port = port
        self._ser = serial.Serial(port, baud, timeout=0.05)
        self._parser = protocol.FrameParser()

    def close(self):
        try:
            self._ser.close()
        except Exception:
            pass

    # ---- 发送 ----
    def send_bytes(self, data):
        self._ser.write(data)

    def send_frame(self, type_str, content):
        self._ser.write(protocol.encode_frame(type_str, content))

    def send_text(self, cmd):
        """Unity WriteLine("//cmd") 语义。"""
        self._ser.write(protocol.text_cmd(cmd))

    def send_line(self, s):
        """裸明文行（如握手 ACK）。"""
        self._ser.write(protocol.plain_line(s))

    # ---- 接收 ----
    def read_msgs(self, deadline_s):
        """持续读到 deadline，返回期间解析出的全部 msg 列表（可能为空）。"""
        out = []
        end = mono() + deadline_s
        while mono() < end:
            n = self._ser.in_waiting
            if n > 0:
                t = mono()
                out.extend(self._parser.feed(self._ser.read(n), t))
            else:
                time.sleep(0.005)
        return out

    def drain(self, seconds=0.2):
        """读并丢弃一段时间内的所有消息。"""
        self.read_msgs(seconds)
