#!/usr/bin/env python3
"""电脑直连 A7670G AT 测试（ESP32 需烧录 passthrough 或 43/44 已释放）"""
import serial
import sys
import time

PORT = "/dev/cu.usbserial-A5069RR4"
BAUDS = (115200, 9600)
CMDS = (b"AT\r\n", b"AT\r\n", b"ATE0\r\n", b"AT+CPIN?\r\n", b"AT+CSQ\r\n")


def main() -> int:
    port = sys.argv[1] if len(sys.argv) > 1 else PORT
    for baud in BAUDS:
        print(f"\n=== {port} @ {baud} ===")
        with serial.Serial(port, baud, timeout=0.5) as s:
            s.setDTR(False)
            s.setRTS(False)
            time.sleep(0.3)
            s.reset_input_buffer()
            for cmd in CMDS:
                s.write(cmd)
                time.sleep(1.2)
                resp = s.read(512)
                print(cmd.decode().strip(), "->", repr(resp))
                if b"OK" in resp:
                    return 0
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
