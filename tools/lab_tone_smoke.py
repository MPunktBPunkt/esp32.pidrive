#!/usr/bin/env python3
"""Lab: tone-source smoke — prefill ring, host-read slot, print msc.tone / stall.

Run on the host that sees the ESP as MSC (e.g. Proxmox):
  python3 tools/lab_tone_smoke.py --esp http://192.168.178.88 --dev /dev/sda
"""
from __future__ import annotations

import argparse
import json
import subprocess
import time
import urllib.request


def get_json(url: str) -> dict:
    with urllib.request.urlopen(url, timeout=10) as r:
        return json.load(r)


def post_json(url: str, body: dict) -> dict:
    req = urllib.request.Request(
        url,
        data=json.dumps(body).encode(),
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=30) as r:
        return json.load(r)


def dd(dev: str, skip: int, count: int, timeout: float = 45.0) -> None:
    """Linux usb-storage hangs on long TinyUSB BUSY — keep reads inside prefill."""
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


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--esp", default="http://192.168.178.88")
    ap.add_argument("--dev", default="/dev/sda")
    ap.add_argument("--uid", default="zuletzt")
    ap.add_argument("--prefill", type=int, default=81920)
    ap.add_argument("--sectors", type=int, default=320, help="160 KiB = 320 sectors")
    args = ap.parse_args()

    st0 = get_json(args.esp + "/api/status")
    print("fw", st0.get("version"), "stall", (st0.get("msc") or {}).get("stall", {}).get("enable"))

    arm = post_json(
        args.esp + "/api/lab/tone_smoke",
        {"uid": args.uid, "prefill": args.prefill},
    )
    print("tone_smoke", json.dumps(arm, separators=(",", ":")))

    st1 = get_json(args.esp + "/api/status")
    slots = (st1.get("msc") or {}).get("slotMap") or []
    slot = next((s for s in slots if s.get("uid") == args.uid), None)
    if not slot:
        raise SystemExit(f"uid {args.uid} not in slotMap")
    lba0 = int(slot["lba0"])
    # Stay inside id3+prefill so Linux host never hits empty-ring BUSY storms.
    covered = int(arm.get("id3Len") or 252) + int(arm.get("prefill") or args.prefill)
    sectors = min(args.sectors, max(16, (covered // 512) - 2))
    print(f"host-read {args.dev} lba={lba0} sectors={sectors} (covered≈{covered}B)")

    t0 = time.time()
    dd(args.dev, lba0, sectors)
    dt = time.time() - t0
    print(f"dd_done {dt:.2f}s")

    time.sleep(0.3)
    st2 = get_json(args.esp + "/api/status")
    m = st2.get("msc") or {}
    print("tone", json.dumps(m.get("tone"), separators=(",", ":")))
    print(
        "stall",
        json.dumps(
            {
                k: (m.get("stall") or {}).get(k)
                for k in (
                    "enable",
                    "armed",
                    "leadBytes",
                    "cmds",
                    "retries",
                    "partials",
                    "timeouts",
                    "waitMsMax",
                )
            },
            separators=(",", ":"),
        ),
    )
    stream = m.get("stream") or {}
    print(
        "stream",
        json.dumps(
            {
                k: stream.get(k)
                for k in ("active", "uid", "id3Len", "size", "liveAvailBytes", "underruns")
            },
            separators=(",", ":"),
        ),
    )

    tone = m.get("tone") or {}
    liveish = (
        int(tone.get("liveRing") or 0)
        + int(tone.get("stallLead") or 0)
        + int(tone.get("stallOk") or 0)
        + int(tone.get("stallPartial") or 0)
        + int(tone.get("id3") or 0)
    )
    und = int(tone.get("underrun") or 0)
    sil = int(tone.get("silenceSlot") or 0)
    ok = liveish > 0 and und == 0
    print("PASS" if ok else "FAIL", f"liveish={liveish} underrun={und} silenceSlotΔ≈{sil}")
    raise SystemExit(0 if ok else 1)


if __name__ == "__main__":
    main()
