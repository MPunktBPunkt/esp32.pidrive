#!/usr/bin/env python3
"""PiDrive ↔ esp32.pidrive PUMP bridge (line-JSON + binary audio over UART).

- Syncs /tmp/pidrive_menu.json via menu_set with soft paging (≤4 MSC slots)
- On play_uid: activate:<uid> + live MP3 (audio_start + ID3/APIC + 0x01 0x55)
- Cover priority: embedded APIC → stations/*.jpg → default.jpg → generated JPEG
- audio_start carries cSrc/cPath/cTry for SoftAP cover-hint UI
- Stations (meta.url) and local_play: paths are streamed via ffmpeg

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
    from mutagen.id3 import ID3, TIT2, TPE1, TALB, APIC
except ImportError:
    ID3 = None  # type: ignore

MENU_PATH = Path("/tmp/pidrive_menu.json")
STATUS_PATH = Path("/tmp/pidrive_status.json")
CMD_PATH = Path("/tmp/pidrive_cmd")
PROBE_PATH = Path("/tmp/pump_id3_probe.mp3")
COVER_ROOTS = [
    Path("/home/pidrive/pidrive/assets/usb-msc-covers"),
    Path.home() / "pidrive" / "assets" / "usb-msc-covers",
    Path(__file__).resolve().parents[2] / "assets" / "usb-msc-covers",
    Path("/home/martin/projects/pidrive/assets/usb-msc-covers"),
]
DEFAULT_COVER_REL = "default.jpg"  # immer, wenn kein Station-/APIC-Cover
MAX_SLOTS = 4
PAGE_CONTENT = 3  # when paging: 3 items + Mehr/Seite1
FRAME_MAX = 480
ID3_BUDGET = 12 * 1024
PAGE_NEXT_UID = "pump:page_next"
PAGE_HOME_UID = "pump:page_home"
AUDIO_EXTS = {".mp3", ".flac", ".ogg", ".m4a", ".aac", ".wav", ".opus", ".wma"}


def inject(cmd: str) -> None:
    CMD_PATH.parent.mkdir(parents=True, exist_ok=True)
    with CMD_PATH.open("a", encoding="utf-8") as f:
        f.write(cmd.rstrip() + "\n")
    print(f"[inject] {cmd}", flush=True)


def read_status() -> dict:
    if not STATUS_PATH.exists():
        return {}
    try:
        return json.loads(STATUS_PATH.read_text(encoding="utf-8"))
    except Exception:
        return {}


def collect_audio_files(path: str) -> list[str]:
    path = os.path.expanduser(path)
    if not path or not os.path.exists(path):
        return []
    if os.path.isfile(path):
        return [path] if os.path.splitext(path)[1].lower() in AUDIO_EXTS else []
    out: list[str] = []
    for root, _, files in os.walk(path):
        for name in sorted(files):
            if os.path.splitext(name)[1].lower() in AUDIO_EXTS:
                out.append(os.path.join(root, name))
    return out


def read_menu_nodes() -> tuple[int, list[dict], dict[str, dict]]:
    """Return (rev, visible nodes in folder order, by_uid)."""
    if not MENU_PATH.exists():
        return 0, [], {}
    data = json.loads(MENU_PATH.read_text(encoding="utf-8"))
    rev = int(data.get("rev") or 0)
    nodes: list[dict] = []
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
        nodes.append(n)
    return rev, nodes, by_uid


def page_items(nodes: list[dict], page: int) -> tuple[list[dict], int]:
    """Build ≤4 slot items; soft-page when more than MAX_SLOTS."""
    if len(nodes) <= MAX_SLOTS:
        items = [
            {
                "uid": str(n["uid"]),
                "name": (n.get("label") or n.get("id") or "?")[:36],
                "kind": n.get("type") or "info",
            }
            for n in nodes[:MAX_SLOTS]
        ]
        return items, 0

    max_page = (len(nodes) - 1) // PAGE_CONTENT
    page = max(0, min(page, max_page))
    start = page * PAGE_CONTENT
    chunk = nodes[start : start + PAGE_CONTENT]
    items = [
        {
            "uid": str(n["uid"]),
            "name": (n.get("label") or n.get("id") or "?")[:36],
            "kind": n.get("type") or "info",
        }
        for n in chunk
    ]
    if page < max_page:
        left = len(nodes) - (start + len(chunk))
        items.append(
            {
                "uid": PAGE_NEXT_UID,
                "name": f"Mehr… (+{left})"[:36],
                "kind": "action",
            }
        )
    elif page > 0:
        items.append(
            {
                "uid": PAGE_HOME_UID,
                "name": "Seite 1",
                "kind": "action",
            }
        )
    return items, page


def cover_candidate_rels(node: dict | None) -> list[str]:
    """Relative paths under assets/usb-msc-covers/ for this menu node."""
    if not node:
        return []
    candidates: list[str] = []
    nid = node.get("id")
    if nid:
        candidates.append(f"stations/{nid}.jpg")
    uid = node.get("uid")
    if uid is not None:
        candidates.append(f"stations/uid_{uid}.jpg")
    label = (node.get("label") or "").lower()
    slug = "".join(c if c.isalnum() else "_" for c in label).strip("_")
    while "__" in slug:
        slug = slug.replace("__", "_")
    if slug:
        candidates.append(f"stations/{slug}.jpg")
    # unique preserve order
    out: list[str] = []
    for c in candidates:
        if c not in out:
            out.append(c)
    return out


def resolve_under_roots(rel: str) -> Path | None:
    for root in COVER_ROOTS:
        if not root.is_dir():
            continue
        p = root / rel
        if p.is_file() and p.stat().st_size > 0:
            return p
    return None


def find_cover_file(node: dict | None) -> Path | None:
    for rel in cover_candidate_rels(node):
        p = resolve_under_roots(rel)
        if p:
            return p
    return None


def find_default_cover() -> Path | None:
    return resolve_under_roots(DEFAULT_COVER_REL)


def cover_hint_for(node: dict | None) -> dict:
    """Human/UI hint: which file to drop in to replace current cover."""
    cands = cover_candidate_rels(node)
    preferred = cands[0] if cands else "stations/<menu_id>.jpg"
    return {
        "folder": "assets/usb-msc-covers/",
        "preferred": preferred,
        "candidates": cands,
        "default": DEFAULT_COVER_REL,
        "repo": "https://github.com/MPunktBPunkt/pidrive/tree/main/assets/usb-msc-covers",
    }


def load_cover(
    node: dict | None, title: str, subtitle: str, footer: str, media_path: str | None = None
) -> tuple[bytes, str, dict]:
    """Return (jpeg_bytes, source_tag, meta).

    Priority: embedded APIC → stations/*.jpg → default.jpg → generated text.
    """
    hint = cover_hint_for(node)
    meta = {
        "src": "none",
        "path": "",
        "rel": "",
        "preferred": hint["preferred"],
        "candidates": hint["candidates"],
        "default": DEFAULT_COVER_REL,
    }
    embedded = extract_apic_from_file(media_path)
    if embedded:
        print(f"[id3] cover from APIC {media_path} ({len(embedded)} B)", flush=True)
        meta.update({"src": "embedded", "path": media_path or "", "rel": ""})
        return embedded, "embedded", meta

    path = find_cover_file(node)
    if path:
        data = resize_jpeg(path.read_bytes())
        # find matching rel
        rel = ""
        for c in hint["candidates"]:
            if path.name == Path(c).name or str(path).endswith(c):
                rel = c
                break
        if not rel:
            rel = f"stations/{path.name}"
        print(f"[id3] cover file {path} ({len(data)} B)", flush=True)
        meta.update({"src": "file", "path": str(path), "rel": rel})
        return data, "file", meta

    default = find_default_cover()
    if default:
        data = resize_jpeg(default.read_bytes())
        print(f"[id3] cover DEFAULT {default} ({len(data)} B)", flush=True)
        meta.update({"src": "default", "path": str(default), "rel": DEFAULT_COVER_REL})
        return data, "default", meta

    jpeg = make_cover_jpeg(title, subtitle, footer)
    print(f"[id3] cover generated text ({len(jpeg)} B) — no {DEFAULT_COVER_REL}", flush=True)
    meta.update({"src": "generated", "path": "", "rel": ""})
    return jpeg, "generated", meta


def resize_jpeg(data: bytes, max_side: int = 320, max_bytes: int = 8000) -> bytes:
    if Image is None:
        return data[:max_bytes] if len(data) > max_bytes else data
    try:
        img = Image.open(io.BytesIO(data)).convert("RGB")
    except Exception:
        return data[:max_bytes] if len(data) > max_bytes else data
    w, h = img.size
    scale = min(1.0, max_side / max(w, h))
    if scale < 1.0:
        img = img.resize((max(1, int(w * scale)), max(1, int(h * scale))), Image.Resampling.LANCZOS)
    for q in (70, 60, 50, 40, 30):
        buf = io.BytesIO()
        img.save(buf, format="JPEG", quality=q, optimize=True)
        out = buf.getvalue()
        if len(out) <= max_bytes:
            return out
    return out  # type: ignore[name-defined]


def extract_apic_from_file(path: str | None) -> bytes | None:
    if not path or not os.path.isfile(path) or ID3 is None:
        return None
    if os.path.splitext(path)[1].lower() != ".mp3":
        return None
    try:
        tags = ID3(path)
    except Exception:
        return None
    for key in tags.keys():
        if key.startswith("APIC"):
            data = getattr(tags[key], "data", None)
            if data:
                return resize_jpeg(data)
    return None


def make_cover_jpeg(title: str, subtitle: str, footer: str) -> bytes:
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
            font_b = ImageFont.truetype(
                "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf", 22
            )
            font = ImageFont.truetype(
                "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf", 16
            )
            font_s = ImageFont.truetype(
                "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf", 13
            )
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
    if ID3 is None:
        raise RuntimeError("python3-mutagen missing")
    raw_mp3 = b"\xff\xfb\x90\x00" + b"\x00" * 200
    PROBE_PATH.write_bytes(raw_mp3)
    tags = ID3()
    tags.delall("APIC")
    tags.add(TIT2(encoding=3, text=title or "PiDrive"))
    tags.add(TPE1(encoding=3, text=artist or "PiDrive"))
    tags.add(TALB(encoding=3, text=album or "USB"))
    tags.add(
        APIC(encoding=3, mime="image/jpeg", type=3, desc="Cover", data=jpeg)
    )
    tags.save(PROBE_PATH)
    data = PROBE_PATH.read_bytes()
    if data[:3] != b"ID3":
        raise RuntimeError("ID3 header missing after mutagen save")
    size = (
        ((data[6] & 0x7F) << 21)
        | ((data[7] & 0x7F) << 14)
        | ((data[8] & 0x7F) << 7)
        | (data[9] & 0x7F)
    )
    tag = data[: 10 + size]
    PROBE_PATH.write_bytes(tag + raw_mp3)
    return tag


def send_bin(ser: serial.Serial, kind: int, payload: bytes) -> None:
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

    def start(
        self,
        uid: str,
        url: str,
        node: dict | None = None,
        media_path: str | None = None,
    ) -> None:
        self.stop()
        self.uid = uid
        # Cover meta filled after load_cover; sent in audio_start below
        cover_meta: dict = {}
        meta = (node or {}).get("meta") or {}
        st = read_status()
        station = (
            meta.get("name")
            or (node or {}).get("label")
            or st.get("radio_name")
            or "Station"
        )
        title = st.get("track") or station
        artist = st.get("artist") or meta.get("genre") or "PiDrive"
        album = st.get("album") or st.get("radio_name") or station
        footer = f"BT:{st.get('bt_device') or '-'} WiFi:{'on' if st.get('wifi') else 'off'}"
        cover_path = media_path or st.get("library_file") or None
        jpeg = b""
        src = "none"
        try:
            jpeg, src, cover_meta = load_cover(
                node, str(station)[:40], f"{artist} — {title}"[:48], footer, cover_path
            )
        except Exception as e:
            print(f"[id3] load_cover: {e}", flush=True)
            cover_meta = cover_hint_for(node)
            cover_meta.update({"src": "error", "path": "", "rel": ""})

        # Prefer short fields for UART line budget (line_[384] on ESP)
        c_rel = (cover_meta.get("rel") or cover_meta.get("preferred") or "")[:72]
        c_try = "|".join((cover_meta.get("candidates") or [])[:3])[:90]
        start_msg = {
            "t": "audio_start",
            "uid": uid,
            "codec": "mp3",
            "br": self.bitrate,
            "cSrc": src[:12],
            "cPath": c_rel,
            "cTry": c_try,
        }
        line = json.dumps(start_msg, separators=(",", ":"))
        self.ser.write((line + "\n").encode())
        self.ser.flush()
        print(f"[tx] {line}", flush=True)

        # Persist hint for SoftAP / humans
        try:
            hint_path = Path("/tmp/pidrive_cover_hint.json")
            hint_path.write_text(
                json.dumps(
                    {
                        "uid": uid,
                        "station": station,
                        "src": src,
                        "rel": cover_meta.get("rel") or "",
                        "path": cover_meta.get("path") or "",
                        "preferred": cover_meta.get("preferred") or c_rel,
                        "candidates": cover_meta.get("candidates") or [],
                        "default": DEFAULT_COVER_REL,
                        "folder": "assets/usb-msc-covers/",
                        "ts": time.time(),
                    },
                    ensure_ascii=False,
                    indent=2,
                ),
                encoding="utf-8",
            )
        except Exception:
            pass

        try:
            if not jpeg:
                d = find_default_cover()
                if d:
                    jpeg = resize_jpeg(d.read_bytes())
                    src = "default"
                else:
                    jpeg = make_cover_jpeg(str(station)[:40], str(title)[:48], footer)
                    src = "generated"
            tag = build_id3_tag(str(title)[:60], str(artist)[:40], str(album)[:40], jpeg)
            if len(tag) > ID3_BUDGET:
                print(f"[id3] tag {len(tag)} B > {ID3_BUDGET}, shrink", flush=True)
                d = find_default_cover()
                if d:
                    jpeg = resize_jpeg(d.read_bytes())
                    src = "default"
                else:
                    jpeg = make_cover_jpeg(str(station)[:40], str(title)[:48], "cover too large")
                    src = "generated"
                tag = build_id3_tag(str(title)[:60], str(artist)[:40], str(album)[:40], jpeg)
            send_bin(self.ser, 0x56, tag[:ID3_BUDGET])
            print(
                f"[id3] sent {min(len(tag), ID3_BUDGET)} B jpeg={len(jpeg)} src={src} "
                f"rel={c_rel!r} → {PROBE_PATH}",
                flush=True,
            )
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


def resolve_stream_target(node: dict) -> tuple[str | None, str | None]:
    """Return (ffmpeg_input, media_path_for_cover)."""
    typ = node.get("type") or ""
    meta = node.get("meta") or {}
    url = meta.get("url") if isinstance(meta, dict) else None
    if typ == "station" and url:
        return str(url), None

    action = node.get("action") or ""
    if isinstance(action, str) and action.startswith("local_play:"):
        payload = action[len("local_play:") :]
        path = payload.split("|", 1)[0].strip()
        files = collect_audio_files(path)
        if not files:
            print(f"[audio] local_play empty: {path}", flush=True)
            return None, None
        # Prefer MP3 for APIC; else first file
        mp3s = [f for f in files if f.lower().endswith(".mp3")]
        chosen = mp3s[0] if mp3s else files[0]
        return chosen, chosen
    return None, None


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
    page = 0
    folder_sig = ""
    buf = b""
    last_menu = 0.0
    by_uid: dict[str, dict] = {}
    sent_audio = 0
    last_audio_log = time.time()
    force_menu = False

    print(
        f"[bridge] {args.port} @ {args.baud} audio={'off' if args.no_audio else args.bitrate} paging=on",
        flush=True,
    )
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
                        if uid == PAGE_NEXT_UID:
                            page += 1
                            force_menu = True
                            print(f"[page] next → {page}", flush=True)
                            continue
                        if uid == PAGE_HOME_UID:
                            page = 0
                            force_menu = True
                            print("[page] home → 0", flush=True)
                            continue

                        # Resolve node BEFORE activate — folder leave removes it from menu.json
                        _, _nodes, by_uid = read_menu_nodes()
                        node = by_uid.get(uid) or {}
                        typ = node.get("type") or ""
                        inject(f"activate:{uid}")
                        if args.no_audio:
                            continue
                        if typ == "folder":
                            audio.stop()
                            page = 0
                            force_menu = True
                            # allow core to rewrite menu.json before next sync
                            time.sleep(0.25)
                            continue

                        src, media_path = resolve_stream_target(node)
                        if src:
                            # brief wait so status/library_file can update for covers
                            time.sleep(0.25)
                            audio.start(uid, src, node, media_path)
                        else:
                            audio.stop()
                            print(
                                f"[audio] no stream target for uid={uid} type={typ} "
                                f"action={node.get('action')}",
                                flush=True,
                            )
                    elif t == "menu_ack":
                        print(f"[menu_ack] ok={msg.get('ok')} n={msg.get('n')}", flush=True)
                    elif t == "audio_ack":
                        print(f"[audio_ack] {msg}", flush=True)
                    elif t == "hello_ack":
                        print(
                            f"[hello_ack] ver={msg.get('ver')} slots={msg.get('slots')} page={msg.get('page')}",
                            flush=True,
                        )

            now = time.time()
            if force_menu or now - last_menu >= args.interval:
                last_menu = now
                rev, nodes, by_uid = read_menu_nodes()
                # folder identity = path labels + node uids (reset page on navigate)
                path_key = ""
                try:
                    raw = json.loads(MENU_PATH.read_text(encoding="utf-8"))
                    path_key = json.dumps(raw.get("path_ids") or raw.get("path") or [], separators=(",", ":"))
                except Exception:
                    path_key = ""
                node_uids = ",".join(str(n.get("uid")) for n in nodes)
                fsig = f"{path_key}|{node_uids}"
                if fsig != folder_sig:
                    folder_sig = fsig
                    page = 0
                items, page = page_items(nodes, page)
                sig = json.dumps(items, separators=(",", ":"))
                if items and (force_menu or rev != last_rev or sig != last_sig):
                    send({"t": "menu_set", "rev": rev, "page": page, "items": items})
                    last_rev = rev
                    last_sig = sig
                force_menu = False

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
