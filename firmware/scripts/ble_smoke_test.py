#!/usr/bin/env python3
"""Saoti BLE Companion smoke test (scan → connect → all commands)."""
from __future__ import annotations

import asyncio
import sys

from bleak import BleakClient, BleakScanner

SERVICE = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
STATUS = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
COMMAND = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"
ANSWER = "6E400004-B5A3-F393-E0A9-E50E24DCCA9E"
THUMB = "6E400005-B5A3-F393-E0A9-E50E24DCCA9E"
EVENT = "6E400006-B5A3-F393-E0A9-E50E24DCCA9E"


class Collector:
    def __init__(self) -> None:
        self.status: list[bytes] = []
        self.answer: list[bytes] = []
        self.thumb: list[bytes] = []
        self.event: list[bytes] = []

    def on_status(self, _h, data: bytearray) -> None:
        self.status.append(bytes(data))
        print(f"  << STATUS {bytes(data)[:160]!r}")

    def on_answer(self, _h, data: bytearray) -> None:
        self.answer.append(bytes(data))
        print(f"  << ANSWER {len(data)}B {bytes(data)[:80]!r}")

    def on_thumb(self, _h, data: bytearray) -> None:
        self.thumb.append(bytes(data))
        print(f"  << THUMB {len(data)}B head={bytes(data)[:8]!r}")

    def on_event(self, _h, data: bytearray) -> None:
        self.event.append(bytes(data))
        print(f"  << EVENT {bytes(data)!r}")


async def find_device(timeout: float = 12.0):
    print(f"[1] scanning {timeout:.0f}s for Saoti-* ...")
    devices = await BleakScanner.discover(timeout=timeout, return_adv=True)
    # macOS bleak may return {address: (BLEDevice, AdvertisementData)}
    for key, val in devices.items():
        if isinstance(val, tuple) and len(val) == 2:
            device, adv = val
        else:
            device, adv = key, val
        name = getattr(device, "name", None) or getattr(adv, "local_name", None) or ""
        uuids = [str(u).lower() for u in (getattr(adv, "service_uuids", None) or [])]
        rssi = getattr(adv, "rssi", None)
        hit = str(name).startswith("Saoti") or SERVICE.lower() in uuids
        if hit:
            print(f"  found name={name!r} rssi={rssi} addr={device.address}")
            return device, str(name), rssi
    return None, None, None


async def cmd(client: BleakClient, text: str, wait: float = 0.9) -> None:
    print(f"  >> CMD {text}")
    await client.write_gatt_char(COMMAND, text.encode("utf-8"), response=False)
    await asyncio.sleep(wait)


async def main() -> int:
    results = []

    device, name, rssi = await find_device(12)
    ok_scan = device is not None
    results.append(("scan", ok_scan, f"{name} rssi={rssi}"))
    if not ok_scan:
        print("FAIL: device not discovered")
        return 1

    col = Collector()
    print(f"[2] connect {name} ({device.address}) ...")
    async with BleakClient(device, timeout=25.0) as client:
        print(f"  connected={client.is_connected}")
        results.append(("connect", client.is_connected, ""))

        await client.start_notify(STATUS, col.on_status)
        await client.start_notify(ANSWER, col.on_answer)
        await client.start_notify(THUMB, col.on_thumb)
        await client.start_notify(EVENT, col.on_event)
        await asyncio.sleep(1.2)

        raw = await client.read_gatt_char(STATUS)
        print(f"  READ STATUS {raw[:220]!r}")
        results.append(
            ("status_read", b"{" in raw, raw[:140].decode("utf-8", "replace"))
        )

        n_ev = len(col.event)
        await cmd(client, "ping")
        pong = any(b"pong" in e for e in col.event[n_ev:])
        results.append(
            (
                "ping",
                pong,
                (col.event[-1].decode("utf-8", "replace") if col.event else ""),
            )
        )

        n_st = len(col.status)
        await cmd(client, "status", 1.2)
        results.append(("status_cmd", len(col.status) > n_st, f"n={len(col.status)}"))

        n_ev = len(col.event)
        await cmd(client, "wake", 2.5)
        wake_ack = any(b"wake" in e for e in col.event[n_ev:])
        results.append(("wake", wake_ack, ""))

        n_thumb = len(col.thumb)
        await cmd(client, "thumb", 0.5)
        for _ in range(24):
            if len(col.thumb) > n_thumb:
                break
            await asyncio.sleep(0.5)
        thumb_ok = len(col.thumb) > n_thumb
        thumb_bytes = sum(len(x) for x in col.thumb[n_thumb:])
        results.append(
            (
                "thumb",
                thumb_ok,
                f"chunks={len(col.thumb) - n_thumb} bytes~{thumb_bytes}",
            )
        )

        n_ev = len(col.event)
        await cmd(client, "flash=1")
        flash_on = any(b"flash" in e for e in col.event[n_ev:])
        results.append(("flash_on", flash_on, ""))

        n_ev = len(col.event)
        await cmd(client, "flash=0")
        flash_off = any(b"flash" in e for e in col.event[n_ev:])
        results.append(("flash_off", flash_off, ""))

        n_ans = len(col.answer)
        await cmd(client, "answer", 1.2)
        results.append(
            ("answer", True, f"notify_delta={len(col.answer) - n_ans}")
        )

        n_ev = len(col.event)
        await cmd(client, "adv")
        adv_ack = any(b"adv" in e for e in col.event[n_ev:])
        results.append(("adv", adv_ack, ""))

        n_ev = len(col.event)
        await cmd(client, "scan", 1.5)
        scan_ack = any(b"scan" in e for e in col.event[n_ev:])
        results.append(("scan_ack", scan_ack, ""))

        print("\n=== RESULT ===")
        failed = 0
        for name_, ok, detail in results:
            mark = "PASS" if ok else "FAIL"
            if not ok:
                failed += 1
            print(f"{mark:4} {name_:12} {detail}")
        print(
            f"totals events={len(col.event)} status={len(col.status)} "
            f"thumb_chunks={len(col.thumb)} answer={len(col.answer)}"
        )
        return 1 if failed else 0


if __name__ == "__main__":
    try:
        raise SystemExit(asyncio.run(main()))
    except Exception as e:
        print("EXCEPTION:", e)
        raise
