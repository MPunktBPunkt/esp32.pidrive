#!/usr/bin/env python3
"""PiDrive ↔ esp32.pidrive PUMP bridge (line-JSON + binary audio over UART or TCP).

- Syncs /tmp/pidrive_menu.json via menu_set with soft paging (≤4 MSC slots)
- On play_uid: activate:<uid> + live MP3 (audio_start + ID3/APIC + 0x01 0x55)
- Cover priority: embedded APIC → stations/*.jpg → default.jpg → generated JPEG
- Status covers: assets/usb-msc-covers/status/*.jpg on stop / idle / no-stream
- Root presets: favorite webradio stations as first MSC page (≤3 + Mehr…)
- audio_start carries cSrc/cPath/cTry for SoftAP cover-hint UI
- Stations (meta.url) and local_play: paths are streamed via ffmpeg
- Special UIDs: pump:stop · pump:favoriten · pump:page_*

Transport:
  UART  --transport uart --port /dev/ttyACM0
  TCP   --transport tcp --host 192.168.4.1 --tcp-port 9090
  auto  UART if port exists, else TCP to --host (SoftAP/STA)

Usage:
  python3 tools/pump_bridge.py [--transport auto|uart|tcp] [--port /dev/ttyACM0]
  python3 tools/pump_bridge.py --transport tcp --host 192.168.4.1 --bitrate 48k
"""
from __future__ import annotations

import argparse
import io
import json
import os
import select
import socket
import subprocess
import sys
import time
from pathlib import Path
from typing import Protocol

try:
    import serial
except ImportError:
    serial = None  # type: ignore

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
STATIONS_JSON_PATHS = [
    Path("/home/pidrive/pidrive/pidrive/config/stations.json"),
    Path("/home/pidrive/pidrive/config/stations.json"),
    Path.home() / "pidrive" / "pidrive" / "config" / "stations.json",
    Path("/home/martin/projects/pidrive/pidrive/config/stations.json"),
]
FAVORITES_JSON_PATHS = [
    Path("/home/pidrive/pidrive/pidrive/config/favorites.json"),
    Path("/home/pidrive/pidrive/config/favorites.json"),
    Path.home() / "pidrive" / "pidrive" / "config" / "favorites.json",
    Path("/home/martin/projects/pidrive/pidrive/config/favorites.json"),
]
DEFAULT_COVER_REL = "default.jpg"  # immer, wenn kein Station-/APIC-Cover
MAX_SLOTS = 4
PAGE_CONTENT = 3  # when paging: 3 items + Mehr/Seite1
FRAME_MAX = 480
ID3_BUDGET = 12 * 1024
PAGE_NEXT_UID = "pump:page_next"
PAGE_HOME_UID = "pump:page_home"
STOP_UID = "pump:stop"
FAVORITEN_UID = "pump:favoriten"
ROOT_UID = "pump:root"
# Default MSC stubs before menu_set — map to root favorite presets
DEMO_UID_TO_FAV = {
    "demo:rock_fm": "fav0",
    "demo:antenne": "fav1",
    "demo:swr3": "fav2",
}
AUDIO_EXTS = {".mp3", ".flac", ".ogg", ".m4a", ".aac", ".wav", ".opus", ".wma"}
# synthetic preset nodes (uid → node), filled when building root presets page
_PRESET_BY_UID: dict[str, dict] = {}


class PumpIO(Protocol):
    def write(self, data: bytes) -> int: ...
    def flush(self) -> None: ...
    def read(self, size: int = 1) -> bytes: ...
    def close(self) -> None: ...
    def reset_input_buffer(self) -> None: ...


class TcpPumpIO:
    """Byte-compatible with pyserial for PUMP framing over WiFi."""

    def __init__(self, host: str, port: int, connect_timeout: float = 8.0):
        self.host = host
        self.port = int(port)
        self._sock = socket.create_connection((host, self.port), timeout=connect_timeout)
        self._sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self._sock.settimeout(0.05)

    def write(self, data: bytes) -> int:
        self._sock.sendall(data)
        return len(data)

    def flush(self) -> None:
        return None

    def read(self, size: int = 1) -> bytes:
        try:
            return self._sock.recv(size) or b""
        except socket.timeout:
            return b""
        except OSError:
            return b""

    def reset_input_buffer(self) -> None:
        self._sock.settimeout(0.01)
        try:
            while True:
                chunk = self._sock.recv(4096)
                if not chunk:
                    break
        except (socket.timeout, OSError):
            pass
        finally:
            self._sock.settimeout(0.05)

    def close(self) -> None:
        try:
            self._sock.close()
        except OSError:
            pass


def _load_pidrive_usb_settings() -> dict:
    candidates = [
        Path("/home/pidrive/pidrive/pidrive/config/settings.json"),
        Path("/home/pidrive/pidrive/config/settings.json"),
        Path.home() / "pidrive" / "pidrive" / "config" / "settings.json",
        Path("/home/martin/projects/pidrive/pidrive/config/settings.json"),
    ]
    for p in candidates:
        if not p.is_file():
            continue
        try:
            return json.loads(p.read_text(encoding="utf-8"))
        except Exception:
            continue
    return {}


def open_pump_link(
    transport: str,
    *,
    port: str,
    baud: int,
    host: str,
    tcp_port: int,
) -> tuple[PumpIO, str]:
    """Return (io, label). transport: uart|tcp|auto."""
    transport = (transport or "auto").strip().lower()
    if transport == "auto":
        if port and os.path.exists(port):
            transport = "uart"
        elif host:
            transport = "tcp"
        else:
            raise SystemExit("auto: weder UART-Port noch --host")

    if transport == "uart":
        if serial is None:
            print("pip/apt install pyserial / python3-serial", file=sys.stderr)
            raise SystemExit(1)
        if not os.path.exists(port):
            raise SystemExit(f"Port fehlt: {port}")
        ser = serial.Serial(port, baud, timeout=0.05)
        time.sleep(0.3)
        ser.reset_input_buffer()
        return ser, f"uart:{port}@{baud}"

    if transport == "tcp":
        if not host:
            raise SystemExit("TCP: --host fehlt (ESP SoftAP 192.168.4.1 oder STA-IP)")
        link = TcpPumpIO(host, tcp_port)
        time.sleep(0.15)
        link.reset_input_buffer()
        return link, f"tcp:{host}:{tcp_port}"

    raise SystemExit(f"unbekanntes --transport {transport!r}")


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
    """Return (rev, visible nodes in folder order, by_uid).

    ``info`` nodes (IP, BT-Status, SSID, …) are included so they appear as
    MSC/SoftAP slot labels. Selecting them must not stop audio.
    """
    if not MENU_PATH.exists():
        return 0, [], {}
    data = json.loads(MENU_PATH.read_text(encoding="utf-8"))
    rev = int(data.get("rev") or 0)
    nodes: list[dict] = []
    by_uid: dict[str, dict] = {}
    for n in data.get("nodes") or []:
        typ = n.get("type") or "info"
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


def _first_existing(paths: list[Path]) -> Path | None:
    for p in paths:
        if p.is_file():
            return p
    return None


def load_preset_stations(limit: int = 6) -> list[dict]:
    """Playable favorite stations for root MSC presets (Favoriten als Slots)."""
    out: list[dict] = []
    seen: set[str] = set()

    fav_path = _first_existing(FAVORITES_JSON_PATHS)
    if fav_path:
        try:
            data = json.loads(fav_path.read_text(encoding="utf-8"))
            for fav in data.get("favorites") or []:
                meta = fav.get("meta") or {}
                url = meta.get("url") or fav.get("url")
                if not url:
                    continue
                sid = str(fav.get("id") or url)
                if sid in seen:
                    continue
                seen.add(sid)
                uid = f"fav{len(out)}"  # short for ESP MenuItem.uid[24]
                out.append(
                    {
                        "uid": uid,
                        "id": sid,
                        "label": (fav.get("name") or sid)[:36],
                        "type": "station",
                        "meta": {"url": url, "name": fav.get("name") or sid, "favorite": True},
                    }
                )
                if len(out) >= limit:
                    return out
        except Exception as e:
            print(f"[presets] favorites.json: {e}", flush=True)

    st_path = _first_existing(STATIONS_JSON_PATHS)
    if st_path:
        try:
            data = json.loads(st_path.read_text(encoding="utf-8"))
            for s in data.get("stations") or []:
                if not s.get("favorite"):
                    continue
                url = s.get("url")
                if not url:
                    continue
                sid = str(s.get("id") or url)
                if sid in seen:
                    continue
                seen.add(sid)
                uid = f"fav{len(out)}"
                out.append(
                    {
                        "uid": uid,
                        "id": sid,
                        "label": (s.get("name") or sid)[:36],
                        "type": "station",
                        "meta": {
                            "url": url,
                            "name": s.get("name") or sid,
                            "genre": s.get("genre") or "",
                            "favorite": True,
                        },
                    }
                )
                if len(out) >= limit:
                    break
        except Exception as e:
            print(f"[presets] stations.json: {e}", flush=True)
    return out


def is_root_menu(path_ids: list | None) -> bool:
    if not path_ids:
        return True
    if len(path_ids) == 1 and str(path_ids[0]) in ("root", "PiDrive", ""):
        return True
    return list(path_ids) == ["root"]


def find_favoriten_uid() -> str | None:
    """Look up Favoriten folder uid from current or walk menu.json nodes."""
    if not MENU_PATH.exists():
        return None
    try:
        data = json.loads(MENU_PATH.read_text(encoding="utf-8"))
    except Exception:
        return None
    for n in data.get("nodes") or []:
        if n.get("id") == "favoriten" or (n.get("label") or "") == "Favoriten":
            if n.get("uid") is not None:
                return str(n.get("uid"))
    return None


def menu_nodes_for_page(path_ids: list | None, nodes: list[dict], page: int) -> tuple[list[dict], int, dict[str, dict]]:
    """Root page 0 = favorite presets as MSC slots (BMW hears stations, not demos once streamed).

    Further pages / non-root = normal soft-paged tree. SoftAP can still navigate via lab/play.
    """
    global _PRESET_BY_UID
    extra: dict[str, dict] = {}
    if is_root_menu(path_ids):
        presets = load_preset_stations(limit=6)
        if presets:
            _PRESET_BY_UID = {str(p["uid"]): p for p in presets}
            if page <= 0:
                chunk = presets[:PAGE_CONTENT]
                items = [
                    {
                        "uid": str(n["uid"]),
                        "name": (n.get("label") or n.get("id") or "?")[:36],
                        "kind": "station",
                    }
                    for n in chunk
                ]
                items.append(
                    {
                        "uid": PAGE_NEXT_UID,
                        "name": "Menü…"[:36],
                        "kind": "action",
                    }
                )
                for n in chunk:
                    extra[str(n["uid"])] = n
                return items, 0, extra
            # page≥1 → normal root folders (page 1 → folders page 0)
            items, p = page_items(nodes, page - 1)
            return items, page, extra
        _PRESET_BY_UID = {}
    else:
        _PRESET_BY_UID = {}
    items, p = page_items(nodes, page)
    return items, p, extra


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
        uid_s = str(uid)
        if uid_s.startswith("fav"):
            pass  # use id below
        else:
            candidates.append(f"stations/uid_{uid_s}.jpg")
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


def find_status_cover(kind: str) -> Path | None:
    """status/<kind>.jpg — wifi, bt_connected, bt_disconnected, dab_scan, idle, no_pi."""
    kind = (kind or "idle").replace("..", "").replace("/", "")
    return resolve_under_roots(f"status/{kind}.jpg")


def infer_status_kind(st: dict | None = None) -> str:
    st = st or read_status()
    src = str(st.get("source") or st.get("active_source") or "").lower()
    if "dab" in src and (st.get("dab_scanning") or st.get("scanning")):
        return "dab_scan"
    wifi = st.get("wifi")
    if wifi is False or wifi == 0 or wifi == "off":
        # only if explicitly off
        pass
    bt = st.get("bt_connected")
    if bt is True or st.get("bt_device"):
        if find_status_cover("bt_connected"):
            return "bt_connected"
    if bt is False and find_status_cover("bt_disconnected"):
        return "bt_disconnected"
    if not st and find_status_cover("no_pi"):
        return "no_pi"
    if find_status_cover("idle"):
        return "idle"
    return "idle"


def load_status_cover_bytes(kind: str | None = None) -> tuple[bytes, str, dict]:
    kind = kind or infer_status_kind()
    path = find_status_cover(kind)
    meta = {
        "src": "status",
        "path": "",
        "rel": f"status/{kind}.jpg",
        "preferred": f"status/{kind}.jpg",
        "candidates": [f"status/{kind}.jpg"],
        "default": DEFAULT_COVER_REL,
    }
    if path:
        data = resize_jpeg(path.read_bytes())
        meta.update({"path": str(path), "rel": f"status/{kind}.jpg"})
        print(f"[id3] cover STATUS {path} ({len(data)} B)", flush=True)
        return data, "status", meta
    default = find_default_cover()
    if default:
        data = resize_jpeg(default.read_bytes())
        meta.update({"src": "default", "path": str(default), "rel": DEFAULT_COVER_REL})
        return data, "default", meta
    jpeg = make_cover_jpeg("Status", kind, "PiDrive")
    meta.update({"src": "generated"})
    return jpeg, "generated", meta


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


def send_bin(ser: PumpIO, kind: int, payload: bytes) -> None:
    off = 0
    while off < len(payload):
        chunk = payload[off : off + FRAME_MAX]
        hdr = bytes((0x01, kind, len(chunk) & 0xFF, (len(chunk) >> 8) & 0xFF))
        ser.write(hdr + chunk)
        off += len(chunk)
    ser.flush()


class AudioFwd:
    def __init__(self, ser: PumpIO, bitrate: str):
        self.ser = ser
        self.bitrate = bitrate
        self.proc: subprocess.Popen | None = None
        self.uid = ""

    def stop(self, status_cover: bool = False) -> None:
        """Stop ffmpeg + ESP stream.

        Do not push_status() after stop on-car: audio_start status:* is not an MSC
        slot and leaves stream.active empty → BMW „keine Einträge“.
        """
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
        if status_cover:
            print("[id3] status cover skipped (MSC-safe)", flush=True)

    def push_status(self, kind: str | None = None) -> None:
        """Sticky ID3+APIC with status/*.jpg so SoftAP still shows a cover when idle."""
        jpeg, src, cover_meta = load_status_cover_bytes(kind)
        kind = kind or infer_status_kind()
        uid = f"status:{kind}"
        self.uid = uid
        c_rel = (cover_meta.get("rel") or "")[:72]
        start_msg = {
            "t": "audio_start",
            "uid": uid,
            "codec": "mp3",
            "br": self.bitrate,
            "cSrc": src[:12],
            "cPath": c_rel,
            "cTry": c_rel,
        }
        line = json.dumps(start_msg, separators=(",", ":"))
        self.ser.write((line + "\n").encode())
        self.ser.flush()
        print(f"[tx] {line}", flush=True)
        try:
            tag = build_id3_tag("PiDrive", kind.replace("_", " "), "Status", jpeg)
            if len(tag) > ID3_BUDGET:
                tag = tag[:ID3_BUDGET]
            send_bin(self.ser, 0x56, tag)  # sticky ID3
            print(f"[id3] status sent {len(tag)} B src={src} rel={c_rel}", flush=True)
        except Exception as e:
            print(f"[id3] status send failed: {e}", flush=True)
        Path("/tmp/pidrive_cover_hint.json").write_text(
            json.dumps(
                {
                    "uid": uid,
                    "station": "Status",
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

    def start(
        self,
        uid: str,
        url: str,
        node: dict | None = None,
        media_path: str | None = None,
    ) -> None:
        self.stop(status_cover=False)
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
        print(f"[trace] audio_start sent uid={uid} cover={src}", flush=True)

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
    cfg = _load_pidrive_usb_settings()
    ap = argparse.ArgumentParser(description="PUMP bridge PiDrive ↔ esp32.pidrive (UART or TCP)")
    ap.add_argument(
        "--transport",
        choices=("auto", "uart", "tcp"),
        default=str(cfg.get("usb_pump_transport") or os.environ.get("PUMP_TRANSPORT") or "auto"),
        help="uart | tcp | auto (UART if port exists, else TCP)",
    )
    ap.add_argument("--port", default=str(cfg.get("usb_pump_port") or "/dev/ttyACM0"))
    ap.add_argument("--baud", type=int, default=int(cfg.get("usb_pump_baud") or 115200))
    ap.add_argument(
        "--host",
        default=str(
            cfg.get("usb_esp_host")
            or os.environ.get("PUMP_HOST")
            or ""
        ).strip(),
        help="ESP SoftAP/STA IP for TCP (default from settings usb_esp_host)",
    )
    ap.add_argument(
        "--tcp-port",
        type=int,
        default=int(cfg.get("usb_pump_tcp_port") or os.environ.get("PUMP_TCP_PORT") or 9090),
    )
    ap.add_argument("--interval", type=float, default=0.5, help="menu poll seconds")
    ap.add_argument("--bitrate", default="48k", help="ffmpeg audio bitrate for USB path")
    ap.add_argument("--no-audio", action="store_true", help="menu/activate only")
    ap.add_argument(
        "--reconnect",
        type=float,
        default=3.0,
        help="seconds between TCP reconnect attempts (0=exit on drop)",
    )
    args = ap.parse_args()

    def connect() -> tuple[PumpIO, str]:
        return open_pump_link(
            args.transport,
            port=args.port,
            baud=args.baud,
            host=args.host,
            tcp_port=args.tcp_port,
        )

    try:
        ser, label = connect()
    except SystemExit as e:
        print(str(e) or "connect failed", file=sys.stderr)
        return 1
    except OSError as e:
        print(f"connect failed: {e}", file=sys.stderr)
        return 1

    def send(obj: dict) -> None:
        line = json.dumps(obj, separators=(",", ":"))
        ser.write((line + "\n").encode("utf-8"))
        ser.flush()
        print(f"[tx] {line}", flush=True)

    audio = AudioFwd(ser, args.bitrate)
    send({"t": "hello", "ver": 1})
    # Always clear any leftover live stream so BMW can index the demo FAT MP3s.
    send({"t": "audio_stop"})
    audio.uid = ""
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
    # Never auto-start audio on plug/scan: NBT reports „keine abspielbaren Titel“
    # while a live stream patches FAT/payload. Demo stubs (~6.5 KiB) need a head
    # re-read past playMinSeqBytes (FW default 6000) for play.guess — Webradio-over-USB
    # also benefits from larger virtual files (separate FW change).
    # Live audio: SoftAP lab/play or play_uid only when the user explicitly starts it
    # *after* titles are listed (and expect a re-open of the track).
    resume_at = 0.0
    play_t0 = None

    print(
        f"[bridge] {label} audio={'off' if args.no_audio else args.bitrate} paging=on",
        flush=True,
    )

    def rebind(new_io: PumpIO) -> None:
        nonlocal ser
        try:
            ser.close()
        except Exception:
            pass
        ser = new_io
        audio.ser = new_io

    def try_reconnect(reason: str) -> bool:
        nonlocal buf, force_menu, last_sig, label
        if args.reconnect <= 0:
            return False
        if args.transport == "uart":
            return False
        if not args.host and args.transport != "auto":
            return False
        print(f"[bridge] link lost ({reason}) — reconnect in {args.reconnect}s", flush=True)
        try:
            audio.stop(status_cover=False)
        except Exception:
            pass
        time.sleep(args.reconnect)
        try:
            new_io, label = connect()
            rebind(new_io)
            buf = b""
            force_menu = True
            last_sig = ""
            send({"t": "hello", "ver": 1})
            send({"t": "audio_stop"})
            print(f"[bridge] reconnected {label}", flush=True)
            return True
        except Exception as e2:
            print(f"[bridge] reconnect failed: {e2}", flush=True)
            return True  # keep looping

    try:
        while True:
            try:
                chunk = ser.read(512)
            except (BrokenPipeError, OSError, ConnectionError) as e:
                if try_reconnect(str(e)):
                    continue
                raise
            if chunk:
                buf += chunk
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    text = line.decode("utf-8", errors="replace").strip()
                    if not text:
                        continue
                    if not text.startswith("{") and not text.startswith("["):
                        if text.startswith("[") or "EVT" in text or "MSC" in text or "UART" in text or "PUMP" in text:
                            print(f"[rx] {text}", flush=True)
                        if "usb.otg.up" in text:
                            page = 0
                            force_menu = True
                            last_sig = ""
                            resume_at = 0.0
                            if audio.uid:
                                audio.stop(status_cover=False)
                            print("[plug] stream off for BMW index (demos)", flush=True)
                        if "usb.otg.down" in text:
                            resume_at = 0.0
                            if audio.uid:
                                audio.stop(status_cover=False)
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
                        play_t0 = time.time()
                        print(f"[trace] t=0ms play_uid={uid}", flush=True)
                        if uid in DEMO_UID_TO_FAV:
                            mapped = DEMO_UID_TO_FAV[uid]
                            print(f"[map] {uid} → {mapped}", flush=True)
                            uid = mapped
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
                        if uid == STOP_UID:
                            audio.stop(status_cover=False)
                            print("[audio] stop (SoftAP/Remote)", flush=True)
                            continue
                        if uid == ROOT_UID:
                            inject("goto:root")
                            page = 0
                            force_menu = True
                            time.sleep(0.3)
                            print("[nav] root", flush=True)
                            continue
                        if uid == FAVORITEN_UID:
                            inject("goto:favoriten")
                            page = 0
                            force_menu = True
                            time.sleep(0.3)
                            print("[nav] Favoriten", flush=True)
                            continue

                        _, _nodes, by_uid = read_menu_nodes()
                        node = by_uid.get(uid) or _PRESET_BY_UID.get(uid) or {}
                        typ = node.get("type") or ""

                        if typ == "info":
                            print(f"[menu] info {(node.get('label') or uid)[:48]}", flush=True)
                            continue

                        if uid.startswith("fav"):
                            if not node:
                                for p in load_preset_stations(limit=6):
                                    _PRESET_BY_UID[str(p["uid"])] = p
                                node = _PRESET_BY_UID.get(uid) or {}
                            if args.no_audio:
                                continue
                            if audio.uid and uid != audio.uid and (time.time() - last_audio_log) < 4.0:
                                print(f"[audio] ignore rapid {uid} (have {audio.uid})", flush=True)
                                continue
                            src, media_path = resolve_stream_target(node) if node else (None, None)
                            if src:
                                time.sleep(0.05)
                                if play_t0 is not None:
                                    print(
                                        f"[trace] t={int((time.time() - play_t0) * 1000)}ms "
                                        f"audio.start fav {uid}",
                                        flush=True,
                                    )
                                audio.start(uid, src, node, media_path)
                            else:
                                audio.stop(status_cover=False)
                                print(f"[audio] fav without url: {uid}", flush=True)
                            continue

                        if not node and uid and not uid.startswith("pump:"):
                            inject(f"activate:{uid}")
                            page = 0
                            force_menu = True
                            time.sleep(0.35)
                            print(f"[nav] stale/unknown uid activate:{uid}", flush=True)
                            continue

                        inject(f"activate:{uid}")
                        if args.no_audio:
                            continue
                        if typ == "folder":
                            audio.stop(status_cover=False)
                            page = 0
                            force_menu = True
                            time.sleep(0.25)
                            continue

                        src, media_path = resolve_stream_target(node)
                        if src:
                            time.sleep(0.25)
                            if play_t0 is not None:
                                print(
                                    f"[trace] t={int((time.time() - play_t0) * 1000)}ms "
                                    f"audio.start {uid}",
                                    flush=True,
                                )
                            audio.start(uid, src, node, media_path)
                        else:
                            audio.stop(status_cover=False)
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
                            f"[hello_ack] ver={msg.get('ver')} slots={msg.get('slots')} "
                            f"page={msg.get('page')} tcp={msg.get('tcp')}",
                            flush=True,
                        )

            now = time.time()
            if force_menu or now - last_menu >= args.interval:
                last_menu = now
                try:
                    rev, nodes, by_uid = read_menu_nodes()
                    path_ids: list = []
                    path_key = ""
                    try:
                        raw = json.loads(MENU_PATH.read_text(encoding="utf-8"))
                        path_ids = list(raw.get("path_ids") or raw.get("path") or [])
                        path_key = json.dumps(path_ids, separators=(",", ":"))
                    except Exception:
                        path_key = ""
                    node_uids = ",".join(str(n.get("uid")) for n in nodes)
                    fsig = f"{path_key}|{node_uids}"
                    if fsig != folder_sig:
                        folder_sig = fsig
                        page = 0
                    items, page, extra = menu_nodes_for_page(path_ids, nodes, page)
                    if extra:
                        by_uid.update(extra)
                    sig = json.dumps(items, separators=(",", ":"))
                    if items and (force_menu or rev != last_rev or sig != last_sig):
                        send({"t": "menu_set", "rev": rev, "page": page, "items": items})
                        last_rev = rev
                        last_sig = sig
                    force_menu = False
                except (BrokenPipeError, OSError, ConnectionError) as e:
                    if try_reconnect(str(e)):
                        continue
                    raise

            try:
                n = 0 if args.no_audio else audio.pump()
            except (BrokenPipeError, OSError, ConnectionError) as e:
                n = 0
                if try_reconnect(str(e)):
                    continue
                raise
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
        try:
            audio.stop()
        except Exception:
            pass
        try:
            ser.close()
        except Exception:
            pass
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
