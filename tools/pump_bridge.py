#!/usr/bin/env python3
"""PiDrive ↔ esp32.pidrive PUMP bridge (line-JSON + binary audio over UART).

- Syncs /tmp/pidrive_menu.json (≤4 slots) via menu_set
- On play_uid: activate:<uid> + optional live MP3 forward (audio_start + 0x01 0x55 frames)
- Station URL from menu node meta.url; re-encoded with ffmpeg to ~48 kbit/s for 115200 baud

Usage:
  python3 tools/pump_bridge.py [--port /dev/ttyACM0] [--baud 115200] [--bitrate 48k]
"""
from __future__ import annotations

import argparse
import json
import os
import select
import subprocess
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
FRAME_MAX = 480


def read_menu() -> tuple[int, list[dict], dict[str, dict]]:
    if not MENU_PATH.exists():
        return 0, [], {}
    data = json.loads(MENU_PATH.read_text(encoding="utf-8"))
    rev = int(data.get("rev") or 0)
    items = []
    by_uid: dict[str, dict] = {}
    for n in data.get("nodes") or []:
        typ = n.get("type") or "info"
        if typ == "info":
            continue
        uid = n.get("uid")
        if uid is None:
            continue
        uid_s = str(uid)
        by_uid[uid_s] = n
        if len(items) < MAX_SLOTS:
            items.append(
                {
                    "uid": uid_s,
                    "name": (n.get("label") or n.get("id") or "?")[:36],
                    "kind": typ,
                }
            )
    return rev, items, by_uid


def inject(cmd: str) -> None:
    CMD_PATH.parent.mkdir(parents=True, exist_ok=True)
    with CMD_PATH.open("a", encoding="utf-8") as f:
        f.write(cmd.rstrip() + "\n")
    print(f"[inject] {cmd}", flush=True)


class AudioFwd:
    def __init__(self, ser: serial.Serial, bitrate: str):
        self.ser = ser
        self.bitrate = bitrate
        self.proc: subprocess.Popen | None = None
        self.uid = ""

    def stop(self) -> None:
        if self.proc and self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self.proc.kill()
        self.proc = None
        if self.uid:
            line = json.dumps({"t": "audio_stop"}, separators=(",", ":"))
            self.ser.write((line + "\n").encode())
            self.ser.flush()
            print(f"[tx] {line}", flush=True)
        self.uid = ""

    def start(self, uid: str, url: str) -> None:
        self.stop()
        self.uid = uid
        line = json.dumps(
            {"t": "audio_start", "uid": uid, "codec": "mp3", "br": self.bitrate},
            separators=(",", ":"),
        )
        self.ser.write((line + "\n").encode())
        self.ser.flush()
        print(f"[tx] {line}", flush=True)
        print(f"[audio] ffmpeg {url} @ {self.bitrate}", flush=True)
        self.proc = subprocess.Popen(
            [
                "ffmpeg",
                "-hide_banner",
                "-loglevel",
                "error",
                "-reconnect",
                "1",
                "-reconnect_streamed",
                "1",
                "-i",
                url,
                "-vn",
                "-ac",
                "1",
                "-ar",
                "22050",
                "-b:a",
                self.bitrate,
                "-f",
                "mp3",
                "pipe:1",
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            bufsize=0,
        )

    def pump(self) -> int:
        """Read ffmpeg stdout and write binary frames. Returns bytes sent."""
        if not self.proc or not self.proc.stdout:
            return 0
        if self.proc.poll() is not None:
            print(f"[audio] ffmpeg exit {self.proc.returncode}", flush=True)
            self.proc = None
            return 0
        r, _, _ = select.select([self.proc.stdout], [], [], 0)
        if not r:
            return 0
        data = self.proc.stdout.read(FRAME_MAX)
        if not data:
            return 0
        # 0x01 0x55 | len_lo | len_hi | payload
        hdr = bytes((0x01, 0x55, len(data) & 0xFF, (len(data) >> 8) & 0xFF))
        self.ser.write(hdr + data)
        return len(data)


def main() -> int:
    ap = argparse.ArgumentParser(description="PUMP UART bridge PiDrive ↔ esp32.pidrive")
    ap.add_argument("--port", default="/dev/ttyACM0")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--interval", type=float, default=0.5, help="menu poll seconds")
    ap.add_argument("--bitrate", default="48k", help="ffmpeg audio bitrate for USB path")
    ap.add_argument("--no-audio", action="store_true", help="menu/activate only")
    args = ap.parse_args()

    if not os.path.exists(args.port):
        print(f"Port fehlt: {args.port}", file=sys.stderr)
        return 1

    ser = serial.Serial(args.port, args.baud, timeout=0.05)
    time.sleep(0.3)
    ser.reset_input_buffer()

    def send(obj: dict) -> None:
        line = json.dumps(obj, separators=(",", ":"))
        ser.write((line + "\n").encode("utf-8"))
        ser.flush()
        print(f"[tx] {line}", flush=True)

    audio = AudioFwd(ser, args.bitrate)
    send({"t": "hello", "ver": 1})
    last_rev = -1
    last_sig = ""
    buf = b""
    last_menu = 0.0
    by_uid: dict[str, dict] = {}
    sent_audio = 0
    last_audio_log = time.time()

    print(f"[bridge] {args.port} @ {args.baud} audio={'off' if args.no_audio else args.bitrate}", flush=True)
    try:
        while True:
            chunk = ser.read(512)
            if chunk:
                buf += chunk
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    text = line.decode("utf-8", errors="replace").strip()
                    if not text:
                        continue
                    # skip binary leftovers that look non-text
                    if not text.startswith("{") and not text.startswith("["):
                        if text.startswith("[") or "EVT" in text or "MSC" in text or "UART" in text:
                            print(f"[rx] {text}", flush=True)
                        continue
                    print(f"[rx] {text}", flush=True)
                    try:
                        msg = json.loads(text)
                    except json.JSONDecodeError:
                        continue
                    t = msg.get("t")
                    if t == "event" and msg.get("op") == "play_uid":
                        uid = str(msg.get("uid") or "")
                        if not uid:
                            continue
                        inject(f"activate:{uid}")
                        if args.no_audio:
                            continue
                        # refresh menu map then resolve URL
                        _, _, by_uid = read_menu()
                        node = by_uid.get(uid) or {}
                        typ = node.get("type") or ""
                        meta = node.get("meta") or {}
                        url = meta.get("url") if isinstance(meta, dict) else None
                        if typ == "station" and url:
                            audio.start(uid, str(url))
                        else:
                            audio.stop()
                    elif t == "menu_ack":
                        print(f"[menu_ack] ok={msg.get('ok')} n={msg.get('n')}", flush=True)
                    elif t == "audio_ack":
                        print(f"[audio_ack] {msg}", flush=True)

            now = time.time()
            if now - last_menu >= args.interval:
                last_menu = now
                rev, items, by_uid = read_menu()
                sig = json.dumps(items, separators=(",", ":"))
                if items and (rev != last_rev or sig != last_sig):
                    send({"t": "menu_set", "rev": rev, "items": items})
                    last_rev = rev
                    last_sig = sig

            n = 0 if args.no_audio else audio.pump()
            sent_audio += n
            if now - last_audio_log >= 2.0:
                if sent_audio:
                    print(f"[audio] forwarded {sent_audio} B / 2s", flush=True)
                sent_audio = 0
                last_audio_log = now

            if n == 0:
                time.sleep(0.01)
    except KeyboardInterrupt:
        print("\n[bridge] stop", flush=True)
    finally:
        audio.stop()
        ser.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
