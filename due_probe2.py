# -*- coding: utf-8 -*-
"""Replicate Unity's exact binary-frame sequence to test whether 'clear' wipes autoStressTest."""
import serial, time

PORT = "COM4"
START, END = 0xAA, 0xDD
TYPE_VC, TYPE_CMD = 0x06, 0x07

def frame(type_id, content):
    body = content.encode()
    return bytes([START, type_id, len(body)]) + body + bytes([END])

def hexdump(data):
    lines = []
    for i in range(0, len(data), 16):
        chunk = data[i:i+16]
        hexpart = ' '.join('%02X' % b for b in chunk)
        asciipart = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
        lines.append('%04X  %-48s  %s' % (i, hexpart, asciipart))
    return '\n'.join(lines) if lines else '(none)'

def collect(sp, seconds, buf):
    t0 = time.time()
    while time.time() - t0 < seconds:
        d = sp.read(4096)
        if d:
            buf.extend(d)

LI4 = bytes([0xAA, 0x00, 0x04, 0x2D, 0x34, 0x3A, 0x31, 0xDD])

def run_case(sp, title, send_clear):
    buf = bytearray()
    sp.write(frame(TYPE_CMD, "autoStressTest"))
    collect(sp, 1.6, buf)                      # readString 1s idle timeout not needed for frames
    if send_clear:
        sp.write(frame(TYPE_CMD, "clear"))
        collect(sp, 1.0, buf)
    sp.write(frame(TYPE_VC, "2=1"))
    collect(sp, 3.0, buf)
    raw = bytes(buf)
    print("=== %s ===" % title)
    print(hexdump(raw))
    print("autoStressTest-start dbg:", b"autoStressTest start" in raw)
    print("echo:2=1:", b"echo:2=1:echo" in raw)
    print("li:-4:1 frame:", LI4 in raw)
    print()

def main():
    sp = serial.Serial(PORT, 115200, timeout=0.05)
    sp.dtr = True
    sp.rts = True
    time.sleep(0.2)
    sp.reset_input_buffer()

    sp.write(b"ACK\r\n")
    hb = bytearray(); collect(sp, 1.5, hb)
    print("handshake ACK_OK:", b"ACK_OK" in bytes(hb)); print()

    run_case(sp, "CASE A: autoStressTest -> 2=1 (no clear)", send_clear=False)
    run_case(sp, "CASE B: autoStressTest -> clear -> 2=1 (Unity ContextInitSync path)", send_clear=True)
    sp.close()

if __name__ == "__main__":
    main()
