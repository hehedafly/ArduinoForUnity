# -*- coding: utf-8 -*-
"""Due COM4 probe: handshake -> //autoStressTest -> //2=1, dump raw bytes."""
import serial, time, sys

PORT = "COM4"

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

def main():
    sp = serial.Serial(PORT, 115200, timeout=0.05)
    sp.dtr = True
    sp.rts = True
    time.sleep(0.2)
    sp.reset_input_buffer()

    # ---- Phase 1: handshake (board spams "initialed:V2.4", wants "ACK") ----
    hbuf = bytearray()
    sp.write(b"ACK\r\n")
    collect(sp, 2.0, hbuf)
    print("=== PHASE1 handshake ===")
    print(hexdump(bytes(hbuf)))
    print("ACK_OK received:", b"ACK_OK" in bytes(hbuf))
    print()

    # ---- Phase 2: send //autoStressTest ----
    b2 = bytearray()
    sp.write(b"//autoStressTest\n")
    collect(sp, 2.5, b2)   # readString() has 1s idle timeout
    print("=== PHASE2 //autoStressTest ===")
    print(hexdump(bytes(b2)))
    print("autoStressTest-start frame seen:", b"autoStressTest start" in bytes(b2))
    print()

    # ---- Phase 3: trial start 2=1 (should trigger delay(500)+AutoLickReport) ----
    b3 = bytearray()
    sp.write(b"//2=1\n")
    collect(sp, 4.0, b3)
    print("=== PHASE3 //2=1 ===")
    print(hexdump(bytes(b3)))
    raw3 = bytes(b3)
    print("echo seen:", b"echo" in raw3)
    print("li:-4:1 frame AA 00 04 2D 34 3A 31 DD seen:",
          bytes([0xAA, 0x00, 0x04, 0x2D, 0x34, 0x3A, 0x31, 0xDD]) in raw3)
    print("li:0:1 frame AA 00 03 30 3A 31 DD seen:",
          bytes([0xAA, 0x00, 0x03, 0x30, 0x3A, 0x31, 0xDD]) in raw3)
    print("debugLog 'trial start' seen:", b"trial start" in raw3)
    print()

    # ---- Phase 4: status query ----
    b4 = bytearray()
    sp.write(b"//check\n")
    collect(sp, 2.5, b4)
    print("=== PHASE4 //check ===")
    print(hexdump(bytes(b4)))

    sp.close()

if __name__ == "__main__":
    main()
