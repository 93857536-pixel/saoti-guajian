#!/usr/bin/env python3
"""Diagnose OV5640 black frames via USB stream protocol (SC01 FE/FC)."""

from __future__ import annotations

import argparse
import struct
import sys
import time
from io import BytesIO

try:
    import serial
except ImportError:
    print("need pyserial: pip install pyserial", file=sys.stderr)
    sys.exit(1)

try:
    from PIL import Image
except ImportError:
    Image = None  # type: ignore


MAGIC_JPEG = bytes([0x53, 0x43, 0x01, 0xFE])
MAGIC_RGB = bytes([0x53, 0x43, 0x01, 0xFC])


def read_exact(ser: serial.Serial, n: int, deadline: float) -> bytes | None:
    buf = bytearray()
    while len(buf) < n:
        if time.time() > deadline:
            return None
        chunk = ser.read(n - len(buf))
        if chunk:
            buf.extend(chunk)
        else:
            time.sleep(0.002)
    return bytes(buf)


def find_magic(ser: serial.Serial, deadline: float) -> bytes | None:
    window = bytearray()
    while time.time() < deadline:
        b = ser.read(1)
        if not b:
            continue
        window += b
        if len(window) > 4:
            del window[:-4]
        if bytes(window) == MAGIC_JPEG or bytes(window) == MAGIC_RGB:
            return bytes(window)
    return None


def rgb565_luma(packet: bytes) -> tuple[int, int, int, int, int]:
    if len(packet) < 4:
        raise ValueError("short rgb packet")
    w = packet[0] | (packet[1] << 8)
    h = packet[2] | (packet[3] << 8)
    pixels = packet[4:]
    need = w * h * 2
    if len(pixels) < need or w < 2 or h < 2:
        raise ValueError(f"bad rgb size {w}x{h} have={len(pixels)}")
    mn, mx, total, count = 255, 0, 0, 0
    step = max(1, (w * h) // 4000)
    for i in range(0, need, 2 * step):
        lo, hi = pixels[i], pixels[i + 1]
        v = lo | (hi << 8)
        r = ((v >> 11) & 0x1F) * 255 // 31
        g = ((v >> 5) & 0x3F) * 255 // 63
        b = (v & 0x1F) * 255 // 31
        y = (r * 30 + g * 59 + b * 11) // 100
        mn = min(mn, y)
        mx = max(mx, y)
        total += y
        count += 1
    return w, h, mn, mx, (total // count if count else 0)


def jpeg_luma(jpeg: bytes) -> tuple[int, int, int, int, int]:
    if Image is None:
        # crude: jpeg size + SOI/EOI only
        return 0, 0, -1, -1, -1
    im = Image.open(BytesIO(jpeg)).convert("RGB")
    w, h = im.size
    px = im.load()
    mn, mx, total, count = 255, 0, 0, 0
    step = max(1, min(w, h) // 40)
    for y in range(0, h, step):
        for x in range(0, w, step):
            r, g, b = px[x, y]
            lum = (r * 30 + g * 59 + b * 11) // 100
            mn = min(mn, lum)
            mx = max(mx, lum)
            total += lum
            count += 1
    return w, h, mn, mx, (total // count if count else 0)


def grab_frames(ser: serial.Serial, n: int, timeout_s: float) -> list[dict]:
    out: list[dict] = []
    deadline = time.time() + timeout_s
    while len(out) < n and time.time() < deadline:
        magic = find_magic(ser, min(deadline, time.time() + 3.0))
        if not magic:
            break
        ln = read_exact(ser, 4, deadline)
        if not ln:
            break
        (plen,) = struct.unpack("<I", ln)
        if plen < 64 or plen > 2_000_000:
            continue
        payload = read_exact(ser, plen, deadline)
        if not payload:
            break
        kind = "jpeg" if magic == MAGIC_JPEG else "rgb565"
        try:
            if kind == "jpeg":
                w, h, mn, mx, avg = jpeg_luma(payload)
            else:
                w, h, mn, mx, avg = rgb565_luma(payload)
        except Exception as e:
            out.append({"kind": kind, "bytes": plen, "err": str(e)})
            continue
        out.append(
            {
                "kind": kind,
                "bytes": plen,
                "w": w,
                "h": h,
                "min": mn,
                "max": mx,
                "avg": avg,
            }
        )
    return out


def drain(ser: serial.Serial, seconds: float = 0.3) -> str:
    end = time.time() + seconds
    text = bytearray()
    while time.time() < end:
        chunk = ser.read(4096)
        if chunk:
            text.extend(chunk)
        else:
            time.sleep(0.01)
    try:
        return text.decode("utf-8", errors="replace")
    except Exception:
        return ""


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/cu.usbserial-A5069RR4")
    ap.add_argument("--baud", type=int, default=115200)
    args = ap.parse_args()

    ser = serial.Serial(args.port, args.baud, timeout=0.05)
    time.sleep(0.2)
    ser.reset_input_buffer()

    print("=== cam black diag ===")
    ser.write(b"?\n")
    time.sleep(0.4)
    status = drain(ser, 0.5)
    for line in status.splitlines():
        if "cam" in line.lower() or line.strip().startswith("{") or "[CAM]" in line or "[USB]" in line:
            print("STATUS:", line[:200])

    # Wake via scan path is heavy; try stream first. If asleep, send s briefly
    # then cancel isn't possible — instead poke with STATUS then V.
    print("start stream (V)...")
    # 不要 drain：二进制帧会和日志混在一起，只能靠 magic 同步抓包
    ser.reset_input_buffer()
    ser.write(b"V")
    time.sleep(1.2)  # warmup

    print("grab live frames...")
    frames = grab_frames(ser, 5, 15.0)
    if not frames:
        print("no frames — camera may be asleep; waking with short scan trigger...")
        ser.write(b"v")  # stop stream during wake
        time.sleep(0.2)
        # wakePeripherals only via s/t — use STATUS then hope user woke; try sending
        # nothing that uploads: we need CAMDIAG. Fallback: read after button wake hint.
        print("HINT: press the scan button once to wake, then re-run this script")
        # Still try V again in case cam was already ready but warmup slow
        ser.reset_input_buffer()
        ser.write(b"V")
        frames = grab_frames(ser, 3, 10.0)

    def summarize(tag: str, fs: list[dict]) -> None:
        print(f"--- {tag}: {len(fs)} frames ---")
        for i, f in enumerate(fs):
            if "err" in f:
                print(f"  #{i} {f['kind']} {f['bytes']}B ERR {f['err']}")
            else:
                print(
                    f"  #{i} {f['kind']} {f['w']}x{f['h']} {f['bytes']}B "
                    f"lum min={f['min']} max={f['max']} avg={f['avg']}"
                )

    summarize("LIVE", frames)

    print("toggle colorbar (C)...")
    ser.write(b"C\n")
    time.sleep(0.8)
    text = drain(ser, 0.8)
    for line in text.splitlines():
        if "colorbar" in line.lower() or "[CAM]" in line:
            print(line[:220])
    ser.reset_input_buffer()
    cb = grab_frames(ser, 3, 10.0)
    summarize("COLORBAR", cb)

    print("toggle colorbar off...")
    ser.write(b"C\n")
    time.sleep(0.5)
    ser.write(b"v")

    # Verdict
    live_avgs = [f["avg"] for f in frames if isinstance(f.get("avg"), int) and f["avg"] >= 0]
    cb_avgs = [f["avg"] for f in cb if isinstance(f.get("avg"), int) and f["avg"] >= 0]
    print("=== verdict ===")
    if not live_avgs and not cb_avgs:
        print("FAIL: no usable frames — wiring/power/init or camera asleep")
        return 2
    if cb_avgs and max(cb_avgs) > 40:
        print("SENSOR OK: colorbar has brightness → sensor+SCCB+DVP path works")
        if live_avgs and max(live_avgs) < 8:
            print("LIVE near-black: exposure/tuning/lens/cover issue (not dead sensor)")
        elif live_avgs and max(live_avgs) < 25:
            print("LIVE very dark: need stronger AEC/AGC or more light")
        else:
            print("LIVE also has signal")
    elif live_avgs and max(live_avgs) < 5 and (not cb_avgs or max(cb_avgs) < 5):
        print("LIKELY HARDWARE: live+colorbar both near-black → check 3V3/DVP/lens or bad module")
    else:
        print(f"partial data live_avg={live_avgs} cb_avg={cb_avgs}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
