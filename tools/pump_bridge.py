#!/usr/bin/env python3
"""PiDrive ↔ esp32.pidrive PUMP bridge (line-JSON over UART).

Syncs current /tmp/pidrive_menu.json view (up to 4 slots) to the ESP MSC gadget
and injects activate:<uid> when the ESP reports play_uid (USB host opened a file
or WebUI lab play).

Usage on Pi:
  python3 tools/pump_bridge.py [--port /dev/ttyACM0] [--baud 115200]

Requires: pyserial (apt: python3-serial)
"""
from __future__ import annotations

import argparse
import json
import os
import sys
import time
from pathlib import Path

try:
    import serial
except ImportError:
    print("pip/apt install pyserial / python3-serial", file=sys.stderr)
    sys.exit(1)

MENU_PATH = Path("/tmp/pidrive_menu.json")
CMD_PATH = Path("/tmp/pidrive_cmd")
MAX_SLOTS = 4


def read_menu_items() -> tuple[int, list[dict]]:
    if not MENU_PATH.exists():
        return 0, []
    data = json.loads(MENU_PATH.read_text(encoding="utf-8"))
    rev = int(data.get("rev") or 0)
    items = []
    for n in data.get("nodes") or []:
        typ = n.get("type") or "info"
        if typ == "info":
            continue
        uid = n.get("uid")
        if uid is None:
            continue
        items.append(
            {
                "uid": str(uid),
                "name": (n.get("label") or n.get("id") or "?")[:36],
                "kind": typ,
            }
        )
        if len(items) >= MAX_SLOTS:
            break
    return rev, items


def inject(cmd: str) -> None:
    CMD_PATH.parent.mkdir(parents=True, exist_ok=True)
    with CMD_PATH.open("a", encoding="utf-8") as f:
        f.write(cmd.rstrip() + "\n")
    print(f"[inject] {cmd}", flush=True)


def main() -> int:
    ap = argparse.ArgumentParser(description="PUMP UART bridge PiDrive ↔ esp32.pidrive")
    ap.add_argument("--port", default="/dev/ttyACM0")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--interval", type=float, default=1.0, help="menu poll seconds")
    args = ap.parse_args()

    if not os.path.exists(args.port):
        print(f"Port fehlt: {args.port}", file=sys.stderr)
        return 1

    ser = serial.Serial(args.port, args.baud, timeout=0.2)
    time.sleep(0.3)
    ser.reset_input_buffer()

    def send(obj: dict) -> None:
        line = json.dumps(obj, separators=(",", ":"))
        ser.write((line + "\n").encode("utf-8"))
        ser.flush()
        print(f"[tx] {line}", flush=True)

    send({"t": "hello", "ver": 1})
    last_rev = -1
    last_sig = ""
    buf = b""

    print(f"[bridge] {args.port} @ {args.baud} — Ctrl+C stop", flush=True)
    try:
        while True:
            chunk = ser.read(256)
            if chunk:
                buf += chunk
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    text = line.decode("utf-8", errors="replace").strip()
                    if not text:
                        continue
                    print(f"[rx] {text}", flush=True)
                    if not text.startswith("{"):
                        continue
                    try:
                        msg = json.loads(text)
                    except json.JSONDecodeError:
                        continue
                    t = msg.get("t")
                    if t == "event" and msg.get("op") == "play_uid":
                        uid = str(msg.get("uid") or "")
                        if uid:
                            inject(f"activate:{uid}")
                    elif t == "menu_ack":
                        print(f"[menu_ack] ok={msg.get('ok')} n={msg.get('n')}", flush=True)

            rev, items = read_menu_items()
            sig = json.dumps(items, separators=(",", ":"))
            if items and (rev != last_rev or sig != last_sig):
                send({"t": "menu_set", "rev": rev, "items": items})
                last_rev = rev
                last_sig = sig

            time.sleep(args.interval)
    except KeyboardInterrupt:
        print("\n[bridge] stop", flush=True)
    finally:
        ser.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
