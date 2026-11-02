#!/usr/bin/env python3
"""Lab: Bridge preroll smoke — TCP audio_start + lavfi sine + preroll, capped host read.

Run on the host that sees the ESP as MSC (e.g. Proxmox):
  python3 tools/lab_live_preroll_smoke.py --host 192.168.178.88 --dev /dev/sda
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
import urllib.request

# Same package dir as pump_bridge
sys.path.insert(0, str(__file__).rsplit("/", 1)[0])
from pump_bridge import (  # noqa: E402
    AudioFwd,
    TcpPumpIO,
    esp_live_avail_bytes,
    fetch_esp_status,
)


def get_json(url: str) -> dict:
    with urllib.request.urlopen(url, timeout=10) as r:
        return json.load(r)


def dd(dev: str, skip: int, count: int, timeout: float = 45.0) -> None:
    subprocess.check_call(
        [
            "dd",
            f"if={dev}",
            "bs=512",
            f"skip={skip}",
            f"count={count}",
            "iflag=direct",
            "status=none",
            "of=/dev/null",
        ],
        stderr=subprocess.DEVNULL,
        timeout=timeout,
    )


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="192.168.178.88")
    ap.add_argument("--tcp-port", type=int, default=9090)
    ap.add_argument("--http-port", type=int, default=80)
    ap.add_argument("--dev", default="/dev/sda")
    ap.add_argument("--uid", default="zuletzt")
    ap.add_argument("--preroll", type=int, default=81920)
    ap.add_argument("--bitrate", default="48k")
    ap.add_argument("--sectors", type=int, default=160, help="capped host read (default 80 KiB)")
    args = ap.parse_args()

    esp_http = f"http://{args.host}:{args.http_port}"
    st0 = get_json(esp_http + "/api/status")
    print(
        "fw",
        st0.get("version"),
        "stall",
        (st0.get("msc") or {}).get("stall", {}).get("enable"),
        "lead",
        (st0.get("msc") or {}).get("stall", {}).get("leadBytes"),
    )

    ser = TcpPumpIO(args.host, args.tcp_port)
    ser.write(b'{"t":"hello","ver":1}\n')
    ser.flush()
    time.sleep(0.4)

    audio = AudioFwd(
        ser,
        args.bitrate,
        preroll_bytes=int(args.preroll),
        esp_host=args.host,
        esp_http_port=int(args.http_port),
        target_bps=12000,
    )
    # Minimal ID3 without APIC bloat for lab (still sticky 0x56 path via start()).
    # Override cover by monkeypatching make path: use start with lavfi URL.
    # AudioFwd.start builds full ID3; for lab speed use a thin path:

    # Stop any prior stream
    ser.write(b'{"t":"audio_stop"}\n')
    ser.flush()
    time.sleep(0.3)

    # Direct start with lavfi sine (preroll inside start)
    node = {"label": "PrerollLab", "meta": {"name": "PrerollLab", "genre": "lab"}}
    lavfi = "lavfi:sine=frequency=880:sample_rate=22050"
    t_start = time.time()
    audio.start(args.uid, lavfi, node=node)
    print(
        f"preroll_sent={audio.preroll_sent} dt={time.time() - t_start:.2f}s",
        flush=True,
    )

    st1 = fetch_esp_status(args.host, args.http_port) or {}
    avail = esp_live_avail_bytes(st1)
    stream = (st1.get("msc") or {}).get("stream") or {}
    print(
        "after_preroll",
        json.dumps(
            {
                "liveAvailBytes": avail,
                "underruns": stream.get("underruns"),
                "uid": stream.get("uid"),
                "active": stream.get("active"),
                "size": stream.get("size"),
            },
            separators=(",", ":"),
        ),
        flush=True,
    )

    slots = (st1.get("msc") or {}).get("slotMap") or []
    slot = next((s for s in slots if s.get("uid") == args.uid), None)
    if not slot:
        print("FAIL no slot for", args.uid, flush=True)
        audio.stop()
        return 1
    lba0 = int(slot["lba0"])
    id3 = int(stream.get("id3Len") or 252)
    covered = id3 + max(audio.preroll_sent, avail, 4096)
    sectors = min(args.sectors, max(16, (covered // 512) - 2))
    print(f"host-read {args.dev} lba={lba0} sectors={sectors} covered≈{covered}B", flush=True)

    # Keep pumping during host read so ring does not drain to underrun
    import threading

    t_dd = time.time()
    dd_done = False
    stop_pump = threading.Event()

    def _bg() -> None:
        while not stop_pump.is_set():
            audio.pump()
            time.sleep(0.005)

    th = threading.Thread(target=_bg, daemon=True)
    th.start()
    try:
        dd(args.dev, lba0, sectors)
        dd_done = True
    finally:
        stop_pump.set()
        th.join(timeout=2.0)
    print(f"dd_done {time.time() - t_dd:.2f}s ok={dd_done}", flush=True)

    # Continue brief fill then sample status
    for _ in range(40):
        audio.pump()
        time.sleep(0.01)
    time.sleep(0.2)
    st2 = fetch_esp_status(args.host, args.http_port) or {}
    m = st2.get("msc") or {}
    tone = m.get("tone") or {}
    stream2 = m.get("stream") or {}
    print("tone", json.dumps(tone, separators=(",", ":")), flush=True)
    print(
        "stream",
        json.dumps(
            {
                k: stream2.get(k)
                for k in ("active", "uid", "id3Len", "size", "liveAvailBytes", "underruns")
            },
            separators=(",", ":"),
        ),
        flush=True,
    )
    print(
        "stall",
        json.dumps(
            {
                k: (m.get("stall") or {}).get(k)
                for k in ("enable", "armed", "leadBytes", "cmds", "partials", "timeouts", "waitMsMax")
            },
            separators=(",", ":"),
        ),
        flush=True,
    )

    und = int(stream2.get("underruns") or tone.get("underrun") or 0)
    avail2 = esp_live_avail_bytes(st2)
    preroll_ok = audio.preroll_sent >= max(4096, args.preroll // 4) or avail >= max(4096, args.preroll // 4)
    ok = dd_done and und == 0 and preroll_ok
    print(
        "PASS" if ok else "FAIL",
        f"preroll_sent={audio.preroll_sent} avail0={avail} avail1={avail2} underrun={und}",
        flush=True,
    )
    audio.stop()
    try:
        ser.write(b'{"t":"audio_stop"}\n')
        ser.flush()
    except Exception:
        pass
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
