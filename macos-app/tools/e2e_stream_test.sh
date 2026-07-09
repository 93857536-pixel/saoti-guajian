#!/bin/bash
# End-to-end USB stream test (mimics SaotiCam connect flow)
set -euo pipefail
PORT="${1:-/dev/cu.usbserial-A5069RR4}"
echo "=== SaotiCam E2E Stream Test ==="
echo "Port: $PORT"
pkill -x SaotiCam 2>/dev/null || true
sleep 1
python3 -u << PY
import serial, struct, time, subprocess, sys
port = "$PORT"
MAGIC_PREFIX = bytes([0x53, 0x43, 0x01])
MAGIC_JPEG = bytes([0x53, 0x43, 0x01, 0xFE])
MAGIC_RGB565 = bytes([0x53, 0x43, 0x01, 0xFC])
ser = serial.Serial(port, 921600, timeout=0.05)
ser.dtr = ser.rts = False
print("[1] Waiting for [USB] ready (max 60s)...")
text = b""
t0 = time.time()
ready = False
while time.time() - t0 < 60:
    chunk = ser.read(4096)
    if chunk:
        text += chunk
        if b"[USB] ready" in text:
            ready = True
            print("[1] ESP32 ready")
            break
        if b"camera=FAIL" in text or b"camera fail" in text:
            print("[FAIL] Camera init failed — unplug ESP32 power and retry")
            sys.exit(2)
    else:
        time.sleep(0.05)
if not ready:
    print("[WARN] Timeout waiting for ready, trying stream anyway...")
ser.write(b"v"); time.sleep(0.2)
ser.reset_input_buffer()
ser.write(b"V")
print("[2] Stream started, reading 10s...")
buf = bytearray(); frames = 0; ok = 0; total = 0; frame_type = "none"
t0 = time.time()
while time.time() - t0 < 10:
    chunk = ser.read(8192)
    if chunk:
        total += len(chunk); buf.extend(chunk)
    while True:
        i = buf.find(MAGIC_PREFIX)
        if i < 0:
            if len(buf) > 4096: del buf[:-128]
            break
        if i > 0: del buf[:i]
        if len(buf) < 8: break
        pkt_type = buf[3]
        if pkt_type not in (0xFE, 0xFC):
            buf.pop(0); continue
        ln = struct.unpack("<I", buf[4:8])[0]
        if ln < 128 or ln > 512000: buf.pop(0); continue
        if len(buf) < 8 + ln: break
        payload = bytes(buf[8:8+ln]); del buf[:8+ln]
        frames += 1
        valid = False
        if pkt_type == 0xFC and len(payload) >= 4:
            w = payload[0] | (payload[1] << 8)
            h = payload[2] | (payload[3] << 8)
            if w > 1 and h > 1 and len(payload) >= 4 + w * h * 2:
                valid = True
                frame_type = "RGB565"
                if ok == 0:
                    print(f"[2] First valid RGB565 frame #{frames} {w}x{h} size={ln}")
        elif pkt_type == 0xFE and len(payload) >= 2 and payload[0] == 0xFF and payload[1] == 0xD8:
            open("/tmp/e2e.jpg", "wb").write(payload)
            try:
                from PIL import Image
                import io
                im = Image.open(io.BytesIO(payload))
                if im.size[0] > 1 and im.size[1] > 1:
                    valid = True
                    frame_type = "JPEG"
                    if ok == 0:
                        print(f"[2] First valid JPEG frame #{frames} {im.size[0]}x{im.size[1]} size={ln}")
            except Exception:
                pass
        if valid:
            ok += 1
ser.write(b"v"); ser.close()
print(f"[3] bytes={total} frames={frames} decoded={ok} type={frame_type}")
if ok > 0:
    print("PASS")
    sys.exit(0)
else:
    print("FAIL")
    sys.exit(1)
PY
