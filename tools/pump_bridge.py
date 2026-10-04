#!/usr/bin/env python3
"""PiDrive ↔ esp32.pidrive PUMP bridge (line-JSON + binary audio over UART or TCP).

- Syncs /tmp/pidrive_menu.json via menu_set with soft paging (≤4 MSC slots)
- P0 MSC session lock: freeze published names/UIDs while USB host is plugged
  (--msc-lock / --no-msc-lock; HTTP poll + usb.otg.* edges)
- On play_uid: activate:<uid> + live MP3 (audio_start + ID3/APIC + 0x01 0x55)
- Cover priority: embedded APIC → stations/*.jpg → default.jpg → generated JPEG
- Status covers: assets/usb-msc-covers/status/*.jpg on stop / idle / no-stream
- Root presets: favorite webradio stations as first MSC page (≤3 + Mehr…)
- audio_start carries cSrc/cPath/cTry for SoftAP cover-hint UI
- Stations (meta.url) and local_play: paths are streamed via ffmpeg
- DAB/FM/ohne URL: Pulse/PipeWire Default-Sink.monitor → ffmpeg → ESP
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
import urllib.request
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
FRAME_MAX = 256
ID3_BUDGET = 8 * 1024
# SoftAP listen needs ≥ realtime fill for 48k MP3 (~6 KB/s). Stay a bit above.
AUDIO_TARGET_BPS = 9000
PULSE_SERVER = "unix:/var/run/pulse/native"
# ffmpeg input marker for Pulse/PipeWire monitor (DAB/FM ohne meta.url)
PULSE_PREFIX = "pulse:"
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
# UIDs from previous menu pages stay resolvable after drill-down / TCP reconnect
# (BMW may re-fire play_uid for a folder that is no longer on the current page).
UID_GRACE_S = 180.0
MSC_LOCK_LOG = Path("/tmp/pidrive_msc_lock.jsonl")


def menu_items_identity(items: list | None) -> list[tuple[str, str]]:
    """Stable (uid, name) tuples for MSC map compare (rev ignored)."""
    out: list[tuple[str, str]] = []
    for it in items or []:
        if not isinstance(it, dict):
            continue
        out.append((str(it.get("uid") or ""), str(it.get("name") or "")))
    return out


class MscSessionLock:
    """Freeze published MSC names/UIDs while USB host session is plugged (P0).

    States: released → sealing (allow one menu_set) → frozen (identical only)
    → released on unplug. Pi-UI may change /tmp/pidrive_menu.json freely; only
    menu_set to the ESP is gated.
    """

    def __init__(self, enabled: bool = True):
        self.enabled = enabled
        self.plugged = False
        self.frozen_items: list[dict] | None = None
        self.frozen_identity: list[tuple[str, str]] | None = None
        self.sealing = False  # True until first successful publish after plug
        self.reject_count = 0
        self.seal_count = 0
        self._last_reject_log = 0.0

    def on_plug(self) -> None:
        if not self.enabled:
            return
        self.plugged = True
        self.frozen_items = None
        self.frozen_identity = None
        self.sealing = True
        print("[msc-lock] USB_SESSION_START → sealing (next menu_set freezes map)", flush=True)
        self._log("session_start", {})

    def on_unplug(self) -> None:
        if not self.enabled:
            return
        was = self.plugged or self.frozen_identity is not None
        self.plugged = False
        self.frozen_items = None
        self.frozen_identity = None
        self.sealing = False
        if was:
            print("[msc-lock] USB_SESSION_END → MSC_MAP_RELEASED", flush=True)
            self._log("session_end", {})

    def set_plugged(self, plugged: bool) -> bool:
        """Update from HTTP poll / events. Returns True if state changed."""
        if not self.enabled:
            self.plugged = plugged
            return False
        if plugged and not self.plugged:
            self.on_plug()
            return True
        if (not plugged) and self.plugged:
            self.on_unplug()
            return True
        return False

    def allow_menu_set(self, items: list) -> tuple[bool, str]:
        """Return (ok, reason). ok=False → defer/reject publish."""
        if not self.enabled:
            return True, "disabled"
        if not self.plugged:
            return True, "unplugged"
        ident = menu_items_identity(items)
        if self.sealing or self.frozen_identity is None:
            return True, "seal"
        if ident == self.frozen_identity:
            return True, "idempotent"
        self.reject_count += 1
        names = ",".join(n for _, n in ident[:4])
        frozen = ",".join(n for _, n in (self.frozen_identity or [])[:4])
        reason = f"frozen_reject want=[{names}] have=[{frozen}]"
        now = time.time()
        if now - self._last_reject_log >= 5.0:
            self._last_reject_log = now
            print(f"[msc-lock] {reason} (n={self.reject_count})", flush=True)
            self._log(
                "reject",
                {"want": ident, "have": self.frozen_identity, "n": self.reject_count},
            )
        return False, reason

    def note_published(self, items: list) -> None:
        """Call after menu_set TX (or menu_ack) while sealing/plugged."""
        if not self.enabled or not self.plugged:
            return
        self.frozen_items = [dict(x) for x in items]
        self.frozen_identity = menu_items_identity(items)
        if self.sealing:
            self.sealing = False
            self.seal_count += 1
            names = ",".join(n for _, n in self.frozen_identity[:4])
            print(f"[msc-lock] MSC_MAP_FROZEN [{names}]", flush=True)
            self._log("frozen", {"items": self.frozen_identity, "seal": self.seal_count})

    def _log(self, op: str, payload: dict) -> None:
        try:
            row = {"ts": time.time(), "op": op, **payload}
            with MSC_LOCK_LOG.open("a", encoding="utf-8") as f:
                f.write(json.dumps(row, separators=(",", ":")) + "\n")
        except OSError:
            pass


def fetch_esp_plugged(host: str, http_port: int = 80, timeout: float = 2.0) -> bool | None:
    """GET /api/status → msc.plugged / otgUp. None on error."""
    if not host:
        return None
    url = f"http://{host}:{http_port}/api/status"
    try:
        with urllib.request.urlopen(url, timeout=timeout) as r:
            doc = json.loads(r.read().decode())
        msc = doc.get("msc") or {}
        if "plugged" in msc:
            return bool(msc.get("plugged"))
        return bool(doc.get("otgUp") or doc.get("usbEnumerated"))
    except Exception:
        return None


def remember_uid_nodes(
    cache: dict[str, tuple[float, dict]],
    nodes: dict[str, dict] | list | None,
) -> None:
    """Merge nodes into uid→(ts, node) grace cache."""
    if not nodes:
        return
    now = time.time()
    if isinstance(nodes, dict):
        iterable = nodes.items()
    else:
        iterable = ((str(n.get("uid")), n) for n in nodes if n and n.get("uid") is not None)
    for uid, node in iterable:
        if not uid or not isinstance(node, dict):
            continue
        cache[str(uid)] = (now, node)


def remember_menu_items(cache: dict[str, tuple[float, dict]], items: list | None) -> None:
    """Remember MSC slot items from a menu_set payload (kind → type)."""
    if not items:
        return
    now = time.time()
    for it in items:
        if not isinstance(it, dict):
            continue
        uid = str(it.get("uid") or "")
        if not uid or uid.startswith("pump:"):
            continue
        cache[uid] = (
            now,
            {
                "uid": uid,
                "type": it.get("kind") or it.get("type") or "station",
                "label": it.get("name") or it.get("label") or uid,
                "action": it.get("action"),
            },
        )


def grace_lookup(cache: dict[str, tuple[float, dict]], uid: str) -> dict | None:
    """Return a remembered node if still within UID_GRACE_S; purge expired."""
    if not uid:
        return None
    now = time.time()
    expired = [k for k, (ts, _) in cache.items() if now - ts > UID_GRACE_S]
    for k in expired:
        del cache[k]
    hit = cache.get(uid)
    return hit[1] if hit else None


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


def fat_safe_name(label: str, limit: int = 36) -> str:
    """FAT-safe MSC slot name: strip favorite '*' and illegal LFN chars.

    BMW/NBT often shows an empty USB list if directory entries contain '*'.
    """
    s = (label or "").strip()
    while s[:1] in ("*", "★", "☆", "\u2605", "\u2606"):
        s = s[1:].lstrip()
    out: list[str] = []
    for ch in s:
        o = ord(ch)
        if o < 0x20 or ch in '"*/:<>?\\|':
            continue
        if ch == "…":
            out.append("...")
            continue
        out.append(ch)
    cleaned = "".join(out).strip(" .")
    if not cleaned:
        cleaned = "Track"
    return cleaned[:limit]


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
                "name": fat_safe_name(n.get("label") or n.get("id") or "?"),
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
            "name": fat_safe_name(n.get("label") or n.get("id") or "?"),
            "kind": n.get("type") or "info",
        }
        for n in chunk
    ]
    if page < max_page:
        left = len(nodes) - (start + len(chunk))
        items.append(
            {
                "uid": PAGE_NEXT_UID,
                "name": fat_safe_name(f"Mehr... (+{left})"),
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
                        "name": fat_safe_name(n.get("label") or n.get("id") or "?"),
                        "kind": "station",
                    }
                    for n in chunk
                ]
                items.append(
                    {
                        "uid": PAGE_NEXT_UID,
                        "name": fat_safe_name("Menue..."),
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


def resize_jpeg(data: bytes, max_side: int = 240, max_bytes: int = 2500) -> bytes:
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
    out = b""
    for q in (70, 60, 50, 40, 30, 25, 20):
        buf = io.BytesIO()
        img.save(buf, format="JPEG", quality=q, optimize=True)
        out = buf.getvalue()
        if len(out) <= max_bytes:
            return out
    # last resort: shrink pixels until under budget
    side = max_side
    while side >= 96 and len(out) > max_bytes:
        side = int(side * 0.75)
        img2 = img.resize((side, side), Image.Resampling.LANCZOS)
        buf = io.BytesIO()
        img2.save(buf, format="JPEG", quality=20, optimize=True)
        out = buf.getvalue()
    return out if len(out) <= max_bytes else out[:max_bytes]


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


def send_bin(ser: PumpIO, kind: int, payload: bytes, *, gap_s: float | None = None) -> None:
    """Send framed binary; batch frames per TCP write.

    ID3 (0x56) stays slow — large sticky headers used to reset WiFiClient.
    Audio (0x55) uses a short gap so SoftAP listen can fill near realtime.
    """
    if gap_s is None:
        gap_s = 0.05 if kind == 0x56 else 0.004
    batch_frames = 2 if kind == 0x56 else 6
    off = 0
    batch = bytearray()
    frames_in_batch = 0
    while off < len(payload):
        chunk = payload[off : off + FRAME_MAX]
        hdr = bytes((0x01, kind, len(chunk) & 0xFF, (len(chunk) >> 8) & 0xFF))
        batch.extend(hdr)
        batch.extend(chunk)
        off += len(chunk)
        frames_in_batch += 1
        if frames_in_batch >= batch_frames or off >= len(payload):
            ser.write(bytes(batch))
            batch.clear()
            frames_in_batch = 0
            if gap_s > 0:
                time.sleep(gap_s)
    ser.flush()


class AudioFwd:
    # Debounce station switches after audio.start — must use started_at, NOT the
    # "forwarded N B / 2s" heartbeat (that refreshed last_audio_log and blocked
    # all fav* switches for the whole stream — field 2026-10-02 ignore rapid).
    RAPID_SWITCH_S = 4.0

    def __init__(self, ser: PumpIO, bitrate: str):
        self.ser = ser
        self.bitrate = bitrate
        self.proc: subprocess.Popen | None = None
        self.uid = ""
        self.started_at: float = 0.0
        self.last_start_msg: dict | None = None
        self.last_id3: bytes = b""
        self.hold_menu_until: float = 0.0

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
        self.started_at = 0.0
        self.last_start_msg = None
        self.last_id3 = b""
        if status_cover:
            print("[id3] status cover skipped (MSC-safe)", flush=True)

    def reannounce(self) -> None:
        """Disabled for TCP: blasting ID3+audio after every drop resets the ESP link
        and blocks menu_set (SoftAP stuck on stale favorites)."""
        print("[id3] reannounce skipped (TCP stability — play again to restore stream)", flush=True)

    def stop_forward_only(self) -> None:
        """Stop ffmpeg→ESP forwarder without touching Pi playback / without audio_stop storm.

        Keep last_start_msg / _last_* so soft_resume can restart the SoftAP stream.
        """
        if self.proc and self.proc.poll() is None:
            try:
                self.proc.terminate()
                self.proc.wait(timeout=2)
            except Exception:
                try:
                    self.proc.kill()
                except Exception:
                    pass
        self.proc = None
        # keep uid / last_start_msg / last_id3 / _last_* for soft_resume
        self.hold_menu_until = 0.0

    def soft_resume(self) -> bool:
        """Disabled: ID3+audio right after reconnect resets ESP TCP (pump.tcp.down storm).

        Play again from SoftAP/BMW once the link is stable.
        """
        if self.last_start_msg or getattr(self, "_last_src", None):
            print(
                "[bridge] soft_resume skipped (TCP stability — play again for SoftAP stream)",
                flush=True,
            )
        self.uid = ""
        self.started_at = 0.0
        self.last_start_msg = None
        self.last_id3 = b""
        self._last_src = None
        return False

    def push_status(self, kind: str | None = None) -> None:
        """Sticky ID3+APIC with status/*.jpg so SoftAP still shows a cover when idle."""
        jpeg, src, cover_meta = load_status_cover_bytes(kind)
        kind = kind or infer_status_kind()
        uid = f"status:{kind}"
        self.uid = uid
        self.started_at = time.time()
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
        self.started_at = time.time()
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
        self.last_start_msg = dict(start_msg)
        self.hold_menu_until = time.time() + 3.0  # MSC presentMedia kills TCP mid-stream
        # Let ESP finish audio_start / audio_ack before binary ID3
        time.sleep(0.2)

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
            tag = tag[:ID3_BUDGET]
            send_bin(self.ser, 0x56, tag)
            self.last_id3 = tag
            # refresh cSrc if shrunk
            self.last_start_msg["cSrc"] = src[:12]
            print(
                f"[id3] sent {len(tag)} B jpeg={len(jpeg)} src={src} "
                f"rel={c_rel!r} → {PROBE_PATH}",
                flush=True,
            )
        except Exception as e:
            print(f"[id3] skip: {e}", flush=True)
            self.last_id3 = b""

        time.sleep(0.5)  # ESP digest sticky ID3 + audio_ack before MP3 flood
        self._last_src = url
        self._last_node = node
        self._last_media = media_path
        cmd, env = _ffmpeg_cmd_for_source(url, self.bitrate)
        print(f"[audio] ffmpeg {url} @ {self.bitrate}", flush=True)
        self.proc = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            env=env,
            bufsize=0,
        )
        self._next_pump_ts = time.time() + 0.2
        self._audio_bytes_window = 0
        self._audio_window_t0 = time.time()

    def pump(self) -> int:
        if not self.proc or not self.proc.stdout:
            return 0
        if self.proc.poll() is not None:
            print(f"[audio] ffmpeg exit {self.proc.returncode}", flush=True)
            self.proc = None
            return 0
        now = time.time()
        if now < getattr(self, "_next_pump_ts", 0):
            return 0
        # Token-bucket toward AUDIO_TARGET_BPS so SoftAP fills near realtime
        # without the old blast that reset ESP TCP.
        t0 = getattr(self, "_audio_window_t0", now)
        sent = getattr(self, "_audio_bytes_window", 0)
        if now - t0 >= 1.0:
            self._audio_window_t0 = now
            self._audio_bytes_window = 0
            t0, sent = now, 0
        if sent >= AUDIO_TARGET_BPS:
            self._next_pump_ts = t0 + 1.0
            return 0
        r, _, _ = select.select([self.proc.stdout], [], [], 0)
        if not r:
            return 0
        # Up to ~1 KiB per tick; bucket caps sustained rate
        want = min(FRAME_MAX * 4, AUDIO_TARGET_BPS - sent)
        data = self.proc.stdout.read(want)
        if not data:
            return 0
        send_bin(self.ser, 0x55, data)
        self._audio_bytes_window = sent + len(data)
        self._next_pump_ts = now + 0.012
        return len(data)


def resolve_pulse_monitor() -> str | None:
    """Default-Sink.monitor for live capture (DAB/FM after activate on Pi)."""
    env = {**os.environ, "PULSE_SERVER": PULSE_SERVER}
    try:
        r = subprocess.run(
            ["pactl", "get-default-sink"],
            capture_output=True,
            text=True,
            timeout=3,
            env=env,
        )
        sink = (r.stdout or "").strip()
    except Exception as e:
        print(f"[audio] pactl default-sink: {e}", flush=True)
        return None
    if not sink:
        print("[audio] kein Default-Sink (Pulse/PipeWire)", flush=True)
        return None
    mon = sink if sink.endswith(".monitor") else f"{sink}.monitor"
    return mon


def _ffmpeg_cmd_for_source(src: str, bitrate: str) -> tuple[list[str], dict]:
    """Build ffmpeg argv + env. pulse:<name> → Pulse input; else URL/file."""
    env = {**os.environ, "PULSE_SERVER": PULSE_SERVER}
    if src.startswith(PULSE_PREFIX):
        mon = src[len(PULSE_PREFIX) :]
        cmd = [
            "ffmpeg",
            "-hide_banner",
            "-loglevel",
            "error",
            "-fflags",
            "nobuffer",
            "-flags",
            "low_delay",
            "-f",
            "pulse",
            "-i",
            mon,
            "-vn",
            "-ac",
            "1",
            "-ar",
            "22050",
            "-b:a",
            bitrate,
            "-f",
            "mp3",
            "pipe:1",
        ]
        return cmd, env
    # HTTP / file / pipe path
    cmd = [
        "ffmpeg",
        "-hide_banner",
        "-loglevel",
        "error",
        "-reconnect",
        "1",
        "-reconnect_streamed",
        "1",
        "-i",
        src,
        "-vn",
        "-ac",
        "1",
        "-ar",
        "22050",
        "-b:a",
        bitrate,
        "-f",
        "mp3",
        "pipe:1",
    ]
    return cmd, env


def resolve_stream_target(node: dict) -> tuple[str | None, str | None]:
    """Return (ffmpeg_input, media_path_for_cover).

    Prefer meta.url / local_play; otherwise Pulse Default-Sink.monitor
    (DAB/FM after activate — Pi plays locally, Bridge forwards to ESP).
    """
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

    # DAB / FM / Scanner / unbekanntes UID nach activate → Pi spielt lokal,
    # Bridge greift Default-Sink.monitor ab (auch wenn node leer / nicht in aktueller Seite).
    mon = resolve_pulse_monitor()
    if mon:
        print(f"[audio] pulse monitor → {mon}", flush=True)
        return f"{PULSE_PREFIX}{mon}", None
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
    ap.add_argument(
        "--msc-lock",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="freeze MSC names/UIDs while USB host is plugged (P0, default on)",
    )
    ap.add_argument(
        "--esp-http-port",
        type=int,
        default=int(cfg.get("usb_esp_port") or os.environ.get("PUMP_HTTP_PORT") or 80),
        help="ESP SoftAP/STA HTTP port for plugged poll",
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
    msc_lock = MscSessionLock(enabled=bool(args.msc_lock))
    last_plug_poll = 0.0
    # Seed lock from live ESP status (bridge often starts mid-session).
    seed = fetch_esp_plugged(args.host, args.esp_http_port)
    if seed is True:
        msc_lock.on_plug()
    elif seed is False:
        msc_lock.on_unplug()
    print(
        f"[msc-lock] enabled={msc_lock.enabled} seed_plugged={seed}",
        flush=True,
    )
    send({"t": "hello", "ver": 1})
    # Wait briefly for hello_ack so menu_set is not raced before the session is up.
    hello_deadline = time.time() + 2.5
    got_hello = False
    boot_buf = b""
    while time.time() < hello_deadline and not got_hello:
        chunk = ser.read(512)
        if chunk:
            boot_buf += chunk
            while b"\n" in boot_buf:
                line, boot_buf = boot_buf.split(b"\n", 1)
                text = line.decode("utf-8", errors="replace").strip()
                if not text:
                    continue
                print(f"[rx] {text}", flush=True)
                try:
                    msg = json.loads(text)
                except json.JSONDecodeError:
                    continue
                if msg.get("t") == "hello_ack":
                    got_hello = True
                    print(
                        f"[hello_ack] ver={msg.get('ver')} slots={msg.get('slots')} "
                        f"page={msg.get('page')} tcp={msg.get('tcp')}",
                        flush=True,
                    )
        else:
            time.sleep(0.02)
    if not got_hello:
        print("[bridge] warn: no hello_ack yet — continuing", flush=True)
    # Always clear any leftover live stream so BMW can index the demo FAT MP3s.
    send({"t": "audio_stop"})
    audio.uid = ""
    last_rev = -1
    last_sig = ""
    page = 0
    folder_sig = ""
    buf = boot_buf  # keep any bytes already read while waiting for hello_ack
    last_menu = 0.0
    last_ping = 0.0
    last_menu_tx = 0.0
    menu_hold_until = 0.0
    # 0 = session ready (steady state); <0 = block menu_set until good hello_ack.
    # Do NOT use now+N deadlines here — that permanently blocked menu_set after N
    # seconds (field 2026-09-30: Pi menu moved, ESP slots stuck).
    hello_ok_until = 0.0
    pending_menu: dict | None = None  # set on tx, cleared on menu_ack
    by_uid: dict[str, dict] = {}
    uid_grace: dict[str, tuple[float, dict]] = {}
    last_menu_snapshot: dict | None = None  # last menu_set payload (for reconnect resync)
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

    def wait_hello_ack(timeout: float = 2.5) -> bool:
        """Drain until hello_ack so menu_set is not raced on a half-open session."""
        nonlocal buf
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                chunk = ser.read(512)
            except Exception:
                return False
            if chunk:
                buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                text = line.decode("utf-8", errors="replace").strip()
                if not text.startswith("{"):
                    continue
                try:
                    msg = json.loads(text)
                except json.JSONDecodeError:
                    continue
                print(f"[rx] {text}", flush=True)
                if msg.get("t") == "hello_ack":
                    print(
                        f"[hello_ack] ver={msg.get('ver')} slots={msg.get('slots')} "
                        f"page={msg.get('page')} tcp={msg.get('tcp')}",
                        flush=True,
                    )
                    return True
            time.sleep(0.05)
        return False

    def try_reconnect(reason: str) -> bool:
        nonlocal buf, force_menu, last_sig, last_rev, label, menu_hold_until, hello_ok_until
        nonlocal pending_menu, last_menu_tx, last_menu_snapshot
        if args.reconnect <= 0:
            return False
        if args.transport == "uart":
            return False
        if not args.host and args.transport != "auto":
            return False
        print(f"[bridge] link lost ({reason}) — reconnect in {args.reconnect}s", flush=True)
        # Stop ESP audio forwarder on drop — reannounce storms reset TCP and freeze menu.
        # Pi radio (welle/mpv) keeps playing locally.
        try:
            audio.stop_forward_only()
        except Exception:
            pass
        time.sleep(args.reconnect)
        try:
            new_io, label = connect()
            rebind(new_io)
            buf = b""
            send({"t": "hello", "ver": 1})
            ok = wait_hello_ack()
            # Keep uid_grace + last_menu_snapshot — wiping them caused stale/unknown
            # when BMW re-fired play_uid for a folder no longer on the current page.
            hello_ok_until = 0.0 if ok else -1.0
            menu_hold_until = time.time() + 0.35
            pending_menu = None
            snap = last_menu_snapshot
            if ok and snap and isinstance(snap, dict) and snap.get("items"):
                try:
                    send(snap)
                    pending_menu = {
                        "rev": int(snap.get("rev") or last_rev or 0),
                        "sig": last_sig or json.dumps(snap.get("items"), separators=(",", ":")),
                        "msg": snap,
                    }
                    last_menu_tx = time.time()
                    remember_menu_items(uid_grace, snap.get("items") or [])
                    force_menu = False
                    print(
                        f"[bridge] reconnected {label} hello_ack=ok "
                        f"(menu snapshot resent rev={snap.get('rev')} "
                        f"grace={len(uid_grace)})",
                        flush=True,
                    )
                except Exception as e_snap:
                    force_menu = True
                    last_sig = ""
                    last_rev = -1
                    print(
                        f"[bridge] reconnected {label} hello_ack=ok "
                        f"(snapshot resend failed: {e_snap}; menu sync pending)",
                        flush=True,
                    )
            else:
                force_menu = True
                last_sig = ""
                last_rev = -1
                print(
                    f"[bridge] reconnected {label} hello_ack={'ok' if ok else 'missing'} "
                    f"(menu sync pending, grace={len(uid_grace)})",
                    flush=True,
                )
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
                            inject("goto:root")
                            force_menu = True
                            last_sig = ""
                            resume_at = 0.0
                            msc_lock.on_plug()
                            if audio.uid:
                                audio.stop(status_cover=False)
                            print("[plug] stream off for BMW index (demos)", flush=True)
                        if "usb.otg.down" in text:
                            resume_at = 0.0
                            msc_lock.on_unplug()
                            if audio.uid:
                                audio.stop(status_cover=False)
                        continue
                    print(f"[rx] {text}", flush=True)
                    try:
                        msg = json.loads(text)
                    except json.JSONDecodeError:
                        continue
                    t = msg.get("t")
                    if t == "event" and msg.get("op") in ("usb.otg.up", "usb.otg.down"):
                        if msg.get("op") == "usb.otg.up":
                            page = 0
                            inject("goto:root")
                            force_menu = True
                            last_sig = ""
                            resume_at = 0.0
                            msc_lock.on_plug()
                            if audio.uid:
                                audio.stop(status_cover=False)
                            print("[plug] stream off for BMW index (demos)", flush=True)
                        else:
                            resume_at = 0.0
                            msc_lock.on_unplug()
                            if audio.uid:
                                audio.stop(status_cover=False)
                        continue
                    if t == "event" and msg.get("op") == "diag":
                        code = str(msg.get("code") or "")
                        detail = str(msg.get("detail") or "")
                        line = f"[msc] {code} {detail}".rstrip()
                        print(line, flush=True)
                        try:
                            with open("/tmp/pidrive_msc_diag.jsonl", "a", encoding="utf-8") as df:
                                df.write(
                                    json.dumps(
                                        {
                                            "ts": time.time(),
                                            "code": code,
                                            "detail": detail,
                                        },
                                        separators=(",", ":"),
                                    )
                                    + "\n"
                                )
                        except OSError:
                            pass
                        continue
                    if t == "event" and msg.get("op") == "msc.reads":
                        # Batched LBA timeline from ESP drain-task (not USB callback)
                        row = {
                            "ts": time.time(),
                            "ms": msg.get("ms"),
                            "gap": msg.get("gap"),
                            "lba0": msg.get("lba0"),
                            "lba1": msg.get("lba1"),
                            "bytes": msg.get("bytes"),
                            "n": msg.get("n"),
                            "kind": msg.get("kind"),
                            "ov": msg.get("ov"),
                            "tag": msg.get("tag") or "",
                        }
                        n = int(msg.get("n") or 0)
                        b = int(msg.get("bytes") or 0)
                        print(
                            f"[msc.reads] n={n} bytes={b} lba={msg.get('lba0')}..{msg.get('lba1')} "
                            f"gap={msg.get('gap')} ov={msg.get('ov')}",
                            flush=True,
                        )
                        try:
                            with open("/tmp/pidrive_msc_reads.jsonl", "a", encoding="utf-8") as rf:
                                rf.write(json.dumps(row, separators=(",", ":")) + "\n")
                        except OSError:
                            pass
                        continue
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
                        remember_uid_nodes(uid_grace, by_uid)
                        node = (
                            by_uid.get(uid)
                            or _PRESET_BY_UID.get(uid)
                            or grace_lookup(uid_grace, uid)
                            or {}
                        )
                        typ = node.get("type") or ""
                        if node and uid not in by_uid and uid not in _PRESET_BY_UID:
                            print(
                                f"[nav] grace hit uid={uid} type={typ or '-'} "
                                f"label={(node.get('label') or '')[:40]}",
                                flush=True,
                            )

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
                            # Debounce only right after a start — not while streaming
                            # (last_audio_log is a 2s forward heartbeat; using it blocked
                            # all station switches for the whole session).
                            age = (
                                (time.time() - audio.started_at)
                                if audio.uid and audio.started_at
                                else 1e9
                            )
                            if audio.uid and uid != audio.uid and age < AudioFwd.RAPID_SWITCH_S:
                                print(
                                    f"[audio] ignore rapid {uid} (have {audio.uid} "
                                    f"age={age:.1f}s<{AudioFwd.RAPID_SWITCH_S:.0f}s)",
                                    flush=True,
                                )
                                continue
                            if audio.uid and uid != audio.uid:
                                print(
                                    f"[audio] switch {audio.uid} → {uid} (age={age:.1f}s)",
                                    flush=True,
                                )
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
                            # Unknown to current page AND grace cache. Still activate so
                            # Pi can navigate, but do NOT start Pulse audio blindly —
                            # folder UIDs after drill-down looked "stale" and wrongly
                            # armed a stream (field 2026-09-28).
                            inject(f"activate:{uid}")
                            page = 0
                            force_menu = True
                            time.sleep(0.35)
                            print(
                                f"[nav] unknown uid activate:{uid} (no grace — no auto audio)",
                                flush=True,
                            )
                            continue

                        if typ == "folder" or typ == "action":
                            # Navigation only — never start SoftAP stream on Zurueck/folders.
                            # Stopping the forwarder keeps Pi radio playing locally.
                            try:
                                audio.stop_forward_only()
                            except Exception:
                                pass
                            # Clean ESP stream state without ID3 blast
                            try:
                                line = json.dumps({"t": "audio_stop"}, separators=(",", ":"))
                                ser.write((line + "\n").encode())
                                ser.flush()
                                print(f"[tx] {line}", flush=True)
                            except Exception as e:
                                print(f"[tx] audio_stop skip: {e}", flush=True)
                            remember_uid_nodes(uid_grace, {uid: node})
                            inject(f"activate:{uid}")
                            page = 0
                            force_menu = True
                            # Let Pi rewrite menu.json; pause audio_stop settle before menu_set
                            time.sleep(1.0)
                            print(
                                f"[nav] {typ}/{node.get('action') or '-'} → activate:{uid}",
                                flush=True,
                            )
                            continue

                        inject(f"activate:{uid}")
                        if args.no_audio:
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
                        print(f"[menu_ack] ok={msg.get('ok')} n={msg.get('n')} rev={msg.get('rev')}", flush=True)
                        if pending_menu and msg.get("ok"):
                            last_rev = int(pending_menu.get("rev") or msg.get("rev") or last_rev)
                            last_sig = str(pending_menu.get("sig") or last_sig)
                            sealed = pending_menu.get("msg", {}).get("items") or []
                            msc_lock.note_published(sealed)
                            pending_menu = None
                        elif pending_menu and msg.get("ok") is False:
                            force_menu = True
                            pending_menu = None
                    elif t == "audio_ack":
                        print(f"[audio_ack] {msg}", flush=True)
                    elif t == "hello_ack":
                        print(
                            f"[hello_ack] ver={msg.get('ver')} slots={msg.get('slots')} "
                            f"page={msg.get('page')} tcp={msg.get('tcp')}",
                            flush=True,
                        )

            now = time.time()
            # Poll ESP HTTP for plugged edge (TCP may not forward usb.otg.* events).
            if msc_lock.enabled and args.host and now - last_plug_poll >= 2.0:
                last_plug_poll = now
                polled = fetch_esp_plugged(args.host, args.esp_http_port)
                if polled is True and not msc_lock.plugged:
                    page = 0
                    inject("goto:root")
                    force_menu = True
                    last_sig = ""
                    msc_lock.set_plugged(True)
                elif polled is False and msc_lock.plugged:
                    msc_lock.set_plugged(False)
            # Keep TCP alive when idle. While streaming, binary frames are enough —
            # ping JSON mid-0x55 has triggered WiFiClient resets on SoftAP+STA.
            if now - last_ping >= 2.0:
                last_ping = now
                if not audio.proc:
                    try:
                        send({"t": "ping"})
                    except (BrokenPipeError, OSError, ConnectionError) as e:
                        if try_reconnect(str(e)):
                            continue
                        raise
            if force_menu or now - last_menu >= args.interval:
                last_menu = now
                try:
                    rev, nodes, by_uid = read_menu_nodes()
                    remember_uid_nodes(uid_grace, by_uid)
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
                        remember_uid_nodes(uid_grace, extra)
                    sig = json.dumps(items, separators=(",", ":"))
                    if items and (force_menu or rev != last_rev or sig != last_sig or pending_menu):
                        # menu_set → MSC slot update on ESP; interleaving with 0x55/0x56
                        # or blasting after every TCP replace freezes SoftAP navigation.
                        streaming = bool(audio.proc) or time.time() < getattr(
                            audio, "hold_menu_until", 0
                        )
                        held = time.time() < menu_hold_until
                        session_ok = hello_ok_until == 0.0
                        rate_ok = (time.time() - last_menu_tx) >= 1.5
                        # Wait for menu_ack before retrying the same payload
                        if pending_menu and (time.time() - last_menu_tx) < 3.0:
                            pass
                        elif (streaming and not force_menu) or held or not session_ok or not rate_ok:
                            pass  # keep force_menu / dirty sig for next tick
                        else:
                            ok_lock, lock_why = msc_lock.allow_menu_set(items)
                            if not ok_lock:
                                # Keep Pi menu dirty; do not update last_sig so we retry after unplug.
                                force_menu = False
                                pass
                            else:
                                msg = {"t": "menu_set", "rev": rev, "page": page, "items": items}
                                send(msg)
                                pending_menu = {"rev": rev, "sig": sig, "msg": msg}
                                last_menu_snapshot = dict(msg)
                                remember_menu_items(uid_grace, items)
                                last_menu_tx = time.time()
                                force_menu = False
                                # Settle — ESP applyMenuSlots while SoftAP+STA is fragile
                                menu_hold_until = time.time() + 1.2
                                if lock_why == "seal":
                                    print(f"[msc-lock] sealing publish ({lock_why})", flush=True)
                    elif not (force_menu or rev != last_rev or sig != last_sig or pending_menu):
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
