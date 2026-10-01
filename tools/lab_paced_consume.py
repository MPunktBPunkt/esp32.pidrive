#!/usr/bin/env python3
"""Lab: paced MSC consume ≈ realtime MP3 (48 kbit/s ≈ 6 KiB/s).

Simulates a cooperative host that keeps reading after select (unlike NBT cache).
Run on Proxmox (ESP as /dev/sdX), bridge streaming to the ESP:

  python3 tools/lab_paced_consume.py --esp http://192.168.178.88 --dev /dev/sda

Modes:
  paced     — continuous realtime read for --seconds
  bmw_hope  — burst → idle → head re-read (B4) → paced (optimistic NBT path)
"""
from __future__ import annotations

import argparse
import json
import subprocess
import time
import urllib.request


def get_status(esp: str) -> dict:
    with urllib.request.urlopen(esp + "/api/status", timeout=5) as r:
        return json.load(r)


def post(esp: str, path: str, body: dict | None = None) -> dict:
    data = None if body is None else json.dumps(body).encode()
    req = urllib.request.Request(
        esp + path,
        data=data,
        headers={"Content-Type": "application/json"} if data else {},
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=10) as r:
        return json.load(r)


def dd_chunk(dev: str, lba: int, sectors: int = 8) -> None:
    for attempt in range(5):
        try:
            subprocess.check_call(
                [
                    "dd",
                    f"if={dev}",
                    "bs=512",
                    f"skip={lba}",
                    f"count={sectors}",
                    "iflag=direct",
                    "status=none",
                    "of=/dev/null",
                ],
                stderr=subprocess.DEVNULL,
            )
            return
        except subprocess.CalledProcessError:
            time.sleep(0.3)
    raise SystemExit(f"dd failed lba={lba}")


def wait_warm(esp: str, warmup: int, timeout: float = 30.0) -> dict:
    t0 = time.time()
    while time.time() - t0 < timeout:
        st = get_status(esp)
        s = st["msc"]["stream"]
        if s.get("active") and int(s.get("size") or 0) >= warmup:
            return st
        time.sleep(0.4)
    raise SystemExit("stream not warm")


def paced_read(
    esp: str,
    dev: str,
    lba0: int,
    seconds: float,
    kib_per_s: float,
) -> dict:
    """Read ~kib_per_s continuously; return deltas."""
    bytes_per_s = kib_per_s * 1024.0
    sectors_per_chunk = 8  # 4 KiB
    chunk_bytes = sectors_per_chunk * 512
    interval = chunk_bytes / bytes_per_s

    before = get_status(esp)["msc"]
    off_sec = 0
    t_end = time.time() + seconds
    n_chunks = 0
    while time.time() < t_end:
        dd_chunk(dev, lba0 + off_sec, sectors_per_chunk)
        off_sec += sectors_per_chunk
        # wrap within first 256 KiB of slot to avoid walking forever off-image
        if off_sec >= 900:  # stay in ~450 KiB of 512 KiB slot
            off_sec = 0
        n_chunks += 1
        time.sleep(max(0.0, interval))
    after = get_status(esp)["msc"]
    sb = after["streamBytes"] - before["streamBytes"]
    ud = after["stream"]["underruns"] - before["stream"]["underruns"]
    rs = int(after["stream"].get("headResyncs") or 0) - int(
        before["stream"].get("headResyncs") or 0
    )
    return {
        "chunks": n_chunks,
        "streamBytes": sb,
        "underruns": ud,
        "live": max(0, sb - ud),
        "headResyncs": rs,
        "absBase": after["stream"].get("absBase"),
        "live_ratio": (max(0, sb - ud) / sb) if sb else 0.0,
    }


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--esp", default="http://192.168.178.88")
    ap.add_argument("--dev", default="/dev/sda")
    ap.add_argument("--mode", choices=("paced", "bmw_hope"), default="bmw_hope")
    ap.add_argument("--seconds", type=float, default=20.0)
    ap.add_argument("--kib-per-s", type=float, default=6.0, help="≈48 kbit/s MP3")
    ap.add_argument("--warmup", type=int, default=8000)
    args = ap.parse_args()

    st = get_status(args.esp)
    slot = st["msc"]["slotMap"][0]
    uid, lba0 = slot["uid"], int(slot["lba0"])
    print(f"fw={st['version']} uid={uid} lba0={lba0} mode={args.mode}")

    if not st["msc"]["stream"].get("active"):
        post(args.esp, "/api/lab/play", {"uid": uid})
    wait_warm(args.esp, args.warmup)
    print("warm", get_status(args.esp)["msc"]["stream"])

    if args.mode == "bmw_hope":
        print("burst 96 KiB…")
        t0 = time.time()
        for i in range(0, 192, 8):
            dd_chunk(args.dev, lba0 + i, 8)
        print(f"burst dt={time.time()-t0:.3f}s")
        print("idle 3s (cache window)…")
        time.sleep(3.0)
        print("B4 head re-read 8 KiB…")
        b0 = get_status(args.esp)["msc"]
        for i in range(0, 16, 8):
            dd_chunk(args.dev, lba0 + i, 8)
        b1 = get_status(args.esp)["msc"]
        print(
            f"  streamBytes+={b1['streamBytes']-b0['streamBytes']} "
            f"underruns+={b1['stream']['underruns']-b0['stream']['underruns']} "
            f"resyncs+={int(b1['stream'].get('headResyncs') or 0)-int(b0['stream'].get('headResyncs') or 0)}"
        )

    print(f"paced {args.seconds}s @ {args.kib_per_s} KiB/s…")
    r = paced_read(args.esp, args.dev, lba0, args.seconds, args.kib_per_s)
    print(
        f"PASS? live_ratio={r['live_ratio']:.3f} live={r['live']} "
        f"streamBytes={r['streamBytes']} underruns={r['underruns']} "
        f"chunks={r['chunks']} absBase={r['absBase']} resyncs+={r['headResyncs']}"
    )
    # Heuristic: at realtime, most served bytes should be live (not silence fill)
    if r["streamBytes"] < 8 * 1024:
        raise SystemExit("FAIL: too few stream bytes")
    if r["live_ratio"] < 0.5:
        raise SystemExit(f"FAIL: live_ratio {r['live_ratio']:.3f} < 0.5")
    print("PASS paced consume")


if __name__ == "__main__":
    main()
