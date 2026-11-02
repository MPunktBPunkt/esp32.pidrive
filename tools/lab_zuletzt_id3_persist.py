#!/usr/bin/env python3
"""Lab: sticky ID3 for zuletzt survives reboot (SPIFFS Id3Store).

  python3 tools/lab_zuletzt_id3_persist.py --host 192.168.178.88
"""
from __future__ import annotations

import argparse
import json
import socket
import struct
import time
import urllib.request


def get_json(url: str) -> dict:
    with urllib.request.urlopen(url, timeout=10) as r:
        return json.load(r)


def post(url: str) -> None:
    req = urllib.request.Request(url, method="POST", data=b"")
    with urllib.request.urlopen(req, timeout=30) as r:
        r.read()


def synchsafe(n: int) -> bytes:
    return bytes(
        [
            (n >> 21) & 0x7F,
            (n >> 14) & 0x7F,
            (n >> 7) & 0x7F,
            n & 0x7F,
        ]
    )


def build_id3_text(title: str, artist: str, album: str) -> bytes:
    """Minimal ID3v2.3 without mutagen (TIT2/TPE1/TALB, UTF-8)."""

    def frame(fid: bytes, text: str) -> bytes:
        payload = b"\x03" + text.encode("utf-8") + b"\x00"
        return fid + struct.pack(">I", len(payload)) + b"\x00\x00" + payload

    body = frame(b"TIT2", title) + frame(b"TPE1", artist) + frame(b"TALB", album)
    return b"ID3\x03\x00\x00" + synchsafe(len(body)) + body


def send_bin(sock: socket.socket, kind: int, payload: bytes) -> None:
    """PUMP binary: 0x01 kind len_lo len_hi payload (len ≤ 512)."""
    off = 0
    while off < len(payload):
        chunk = payload[off : off + 256]
        hdr = bytes([0x01, kind, len(chunk) & 0xFF, (len(chunk) >> 8) & 0xFF])
        sock.sendall(hdr + chunk)
        off += len(chunk)
        time.sleep(0.01)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="192.168.178.88")
    ap.add_argument("--tcp-port", type=int, default=9090)
    ap.add_argument("--http-port", type=int, default=80)
    args = ap.parse_args()
    base = f"http://{args.host}:{args.http_port}"

    st0 = get_json(base + "/api/status")
    print("fw", st0.get("version"), "zuletztId3", st0.get("zuletztId3"))

    tag = build_id3_text("ZuletztPersist", "PiDriveLab", "BootCover")
    print(f"id3_tag {len(tag)} B", flush=True)

    sock = socket.create_connection((args.host, args.tcp_port), timeout=8)
    sock.settimeout(5)
    sock.sendall(b'{"t":"hello","ver":1}\n')
    time.sleep(0.3)
    sock.sendall(b'{"t":"audio_start","uid":"zuletzt","codec":"mp3","br":"48k"}\n')
    time.sleep(0.3)
    send_bin(sock, 0x56, tag)
    # One silence-ish MP3 frame so capture-on-0x55 fires; minimal MPEG frame header
    frame = bytes([0xFF, 0xFB, 0x90, 0x00]) + bytes(140)
    send_bin(sock, 0x55, frame)
    time.sleep(0.8)
    sock.sendall(b'{"t":"audio_stop"}\n')
    time.sleep(0.5)
    sock.close()

    st1 = get_json(base + "/api/status")
    z1 = st1.get("zuletztId3") or {}
    print("after_push", z1, flush=True)
    if int(z1.get("len") or 0) < 10 or not z1.get("hasCover"):
        # hasCover = ID3 magic; no APIC required
        if int(z1.get("len") or 0) < 10:
            print("FAIL no persist after push", flush=True)
            return 1

    print("restart…", flush=True)
    try:
        post(base + "/api/restart")
    except Exception as e:
        print("restart req:", e, flush=True)

    # Wait for SoftAP/STA back
    st2 = None
    for i in range(40):
        time.sleep(1.5)
        try:
            st2 = get_json(base + "/api/status")
            if st2.get("version"):
                break
        except Exception:
            st2 = None
            print(f"  wait {i + 1}…", flush=True)
    if not st2:
        print("FAIL no status after reboot", flush=True)
        return 1

    z2 = st2.get("zuletztId3") or {}
    print("after_boot", st2.get("version"), z2, flush=True)
    ok = int(z2.get("len") or 0) >= 10 and int(z2.get("len") or 0) == int(z1.get("len") or 0)
    print("PASS" if ok else "FAIL", f"len {z1.get('len')} → {z2.get('len')}", flush=True)
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
