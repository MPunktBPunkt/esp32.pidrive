#!/usr/bin/env python3
"""PiDrive ↔ esp32.pidrive PUMP bridge (line-JSON + binary audio over UART).

- Syncs /tmp/pidrive_menu.json (≤4 slots) via menu_set
- On play_uid: activate:<uid> + live MP3 (audio_start + ID3/APIC + 0x01 0x55 frames)
- Station URL from menu node meta.url; ffmpeg → ~48 kbit/s
- ID3v2 + generated cover JPEG (sticky on ESP via 0x01 0x56 frames)

Usage:
  python3 tools/pump_bridge.py [--port /dev/ttyACM0] [--baud 115200] [--bitrate 48k]
"""
from __future__ import annotations

import argparse
import io
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

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    Image = None  # type: ignore

try:
    from mutagen.id3 import ID3, TIT2, TPE1, TALB, APIC, ID3NoHeaderError
    from mutagen.mp3 import MP3
except ImportError:
    ID3 = None  # type: ignore

MENU_PATH = Path("/tmp/pidrive_menu.json")
STATUS_PATH = Path("/tmp/pidrive_status.json")
CMD_PATH = Path("/tmp/pidrive_cmd")
PROBE_PATH = Path("/tmp/pump_id3_probe.mp3")
# Custom covers (pidrive repo on the Pi)
COVER_ROOTS = [
    Path("/home/pidrive/pidrive/assets/usb-msc-covers"),
    Path.home() / "pidrive" / "assets" / "usb-msc-covers",
    Path(__file__).resolve().parents[2] / "assets" / "usb-msc-covers",
]
MAX_SLOTS = 4
FRAME_MAX = 480
ID3_BUDGET = 12 * 1024  # must match ESP StreamBuffer::kId3Max


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


def read_status() -> dict:
    if not STATUS_PATH.exists():
        return {}
    try:
        return json.loads(STATUS_PATH.read_text(encoding="utf-8"))
    except Exception:
        return {}


def inject(cmd: str) -> None:
    CMD_PATH.parent.mkdir(parents=True, exist_ok=True)
    with CMD_PATH.open("a", encoding="utf-8") as f:
        f.write(cmd.rstrip() + "\n")
    print(f"[inject] {cmd}", flush=True)


def find_cover_file(node: dict | None) -> Path | None:
    """Look up stations/<id>.jpg or uid_<uid>.jpg under cover roots."""
    if not node:
        return None
    candidates: list[str] = []
    nid = node.get("id")
    if nid:
        candidates.append(f"stations/{nid}.jpg")
    uid = node.get("uid")
    if uid is not None:
        candidates.append(f"stations/uid_{uid}.jpg")
    label = (node.get("label") or "").lower()
    slug = "".join(c if c.isalnum() else "_" for c in label).strip("_")
    if slug:
        candidates.append(f"stations/{slug}.jpg")
    for root in COVER_ROOTS:
        if not root.is_dir():
            continue
        for rel in candidates:
            p = root / rel
            if p.is_file() and p.stat().st_size > 0:
                return p
    return None


def load_or_make_cover(node: dict | None, title: str, subtitle: str, footer: str) -> bytes:
    path = find_cover_file(node)
    if path:
        data = path.read_bytes()
        if len(data) > 10_000:
            print(f"[id3] cover large {len(data)} B: {path}", flush=True)
        print(f"[id3] cover file {path} ({len(data)} B)", flush=True)
        return data
    return make_cover_jpeg(title, subtitle, footer)


def make_cover_jpeg(title: str, subtitle: str, footer: str) -> bytes:
    """Small JPEG for ID3 APIC / BMW-ish size."""
    if Image is None:
        return (
            b"\xff\xd8\xff\xe0\x00\x10JFIF\x00\x01\x01\x00\x00\x01\x00\x01\x00\x00"
            b"\xff\xdb\x00C\x00\x08\x06\x06\x07\x06\x05\x08\x07\x07\x07\t\t"
            b"\x08\n\x0c\x14\r\x0c\x0b\x0b\x0c\x19\x12\x13\x0f\x14\x1d\x1a"
            b"\x1f\x1e\x1d\x1a\x1c\x1c $.\' \",#\x1c\x1c(7),01444\x1f\'9=82<.342"
            b"\xff\xc0\x00\x0b\x08\x00\x01\x00\x01\x01\x01\x11\x00"
            b"\xff\xc4\x00\x1f\x00\x00\x01\x05\x01\x01\x01\x01\x01\x01\x00\x00"
            b"\x00\x00\x00\x00\x00\x00\x01\x02\x03\x04\x05\x06\x07\x08\t\n\x0b"
            b"\xff\xda\x00\x08\x01\x01\x00\x00?\x00\x7f\xff\xd9"
        )
    w = h = 320
    img = Image.new("RGB", (w, h), (12, 28, 48))
    draw = ImageDraw.Draw(img)
    draw.rectangle([0, 0, w, 48], fill=(30, 90, 160))
    draw.rectangle([0, h - 40, w, h], fill=(20, 50, 80))
    try:
        font_b = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 22)
        font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 16)
        font_s = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 13)
    except Exception:
        try:
            font_b = ImageFont.truetype("/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf", 22)
            font = ImageFont.truetype("/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf", 16)
            font_s = ImageFont.truetype("/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf", 13)
        except Exception:
            font_b = font = font_s = ImageFont.load_default()
    draw.text((12, 12), "PiDrive", fill=(220, 235, 255), font=font_b)
    draw.text((12, 90), (title or "Station")[:28], fill=(255, 255, 255), font=font_b)
    draw.text((12, 130), (subtitle or "")[:36], fill=(180, 210, 240), font=font)
    draw.text((12, h - 28), (footer or "")[:40], fill=(160, 190, 220), font=font_s)
    buf = io.BytesIO()
    img.save(buf, format="JPEG", quality=70, optimize=True)
    return buf.getvalue()


def build_id3_tag(title: str, artist: str, album: str, jpeg: bytes) -> bytes:
    """Return raw ID3v2 tag bytes (no audio)."""
    if ID3 is None:
        raise RuntimeError("python3-mutagen missing")
    # mutagen wants a file; build empty MP3 then strip audio
    raw_mp3 = (
        b"\xff\xfb\x90\x00" + b"\x00" * 200
    )  # tiny frame-ish padding; we'll take only ID3
    PROBE_PATH.write_bytes(raw_mp3)
    try:
        tags = ID3()
    except Exception:
        tags = ID3()
    tags.delall("APIC")
    tags.add(TIT2(encoding=3, text=title or "PiDrive"))
    tags.add(TPE1(encoding=3, text=artist or "PiDrive"))
    tags.add(TALB(encoding=3, text=album or "USB"))
    tags.add(
        APIC(
            encoding=3,
            mime="image/jpeg",
            type=3,
            desc="Cover",
            data=jpeg,
        )
    )
    tags.save(PROBE_PATH)
    data = PROBE_PATH.read_bytes()
    if data[:3] != b"ID3":
        raise RuntimeError("ID3 header missing after mutagen save")
    # ID3 size synchsafe at bytes 6..9
    size = (
        ((data[6] & 0x7F) << 21)
        | ((data[7] & 0x7F) << 14)
        | ((data[8] & 0x7F) << 7)
        | (data[9] & 0x7F)
    )
    total = 10 + size
    tag = data[:total]
    # keep probe = tag + tiny silence for local ffprobe
    PROBE_PATH.write_bytes(tag + raw_mp3)
    return tag


def send_bin(ser: serial.Serial, kind: int, payload: bytes) -> None:
    """kind 0x55 audio, 0x56 sticky ID3 append."""
    off = 0
    while off < len(payload):
        chunk = payload[off : off + FRAME_MAX]
        hdr = bytes((0x01, kind, len(chunk) & 0xFF, (len(chunk) >> 8) & 0xFF))
        ser.write(hdr + chunk)
        off += len(chunk)
    ser.flush()


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

    def start(self, uid: str, url: str, node: dict | None = None) -> None:
        self.stop()
        self.uid = uid
        line = json.dumps(
            {"t": "audio_start", "uid": uid, "codec": "mp3", "br": self.bitrate},
            separators=(",", ":"),
        )
        self.ser.write((line + "\n").encode())
        self.ser.flush()
        print(f"[tx] {line}", flush=True)

        meta = (node or {}).get("meta") or {}
        st = read_status()
        station = (
            meta.get("name")
            or (node or {}).get("label")
            or st.get("radio_name")
            or "Station"
        )
        title = st.get("track") or station
        artist = st.get("artist") or meta.get("genre") or "Webradio"
        album = st.get("radio_name") or station
        footer = f"BT:{st.get('bt_device') or '-'} WiFi:{'on' if st.get('wifi') else 'off'}"
        try:
            jpeg = load_or_make_cover(node, str(station)[:40], f"{artist} — {title}"[:48], footer)
            tag = build_id3_tag(str(title)[:60], str(artist)[:40], str(album)[:40], jpeg)
            if len(tag) > ID3_BUDGET:
                print(f"[id3] tag {len(tag)} B > {ID3_BUDGET}, skip APIC / shrink", flush=True)
                # retry without custom image
                jpeg = make_cover_jpeg(str(station)[:40], str(title)[:48], "cover too large")
                tag = build_id3_tag(str(title)[:60], str(artist)[:40], str(album)[:40], jpeg)
            send_bin(self.ser, 0x56, tag[:ID3_BUDGET])
            print(f"[id3] sent {min(len(tag), ID3_BUDGET)} B (jpeg {len(jpeg)} B) → {PROBE_PATH}", flush=True)
        except Exception as e:
            print(f"[id3] skip: {e}", flush=True)

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
        send_bin(self.ser, 0x55, data)
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
                        _, _, by_uid = read_menu()
                        node = by_uid.get(uid) or {}
                        typ = node.get("type") or ""
                        meta = node.get("meta") or {}
                        url = meta.get("url") if isinstance(meta, dict) else None
                        if typ == "station" and url:
                            audio.start(uid, str(url), node)
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
