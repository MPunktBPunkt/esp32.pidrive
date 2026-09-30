#!/usr/bin/env python3
"""Lab host consume test: lab/play → wait warm ring → burst-read MSC slot.

Run on the machine that has the ESP as /dev/sdX (Proxmox host):
  python3 tools/lab_overlay_consume.py --esp http://192.168.178.88 --dev /dev/sda
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


def dd_read(dev: str, lba: int, sectors: int, chunk: int = 8) -> None:
    for off in range(0, sectors, chunk):
        c = min(chunk, sectors - off)
        for attempt in range(5):
            try:
                subprocess.check_call(
                    [
                        "dd",
                        f"if={dev}",
                        "bs=512",
                        f"skip={lba + off}",
                        f"count={c}",
                        "iflag=direct",
                        "status=none",
                        "of=/dev/null",
                    ],
                    stderr=subprocess.DEVNULL,
                )
                break
            except subprocess.CalledProcessError:
                time.sleep(0.4)
                if attempt == 4:
                    raise


def wait_dev(dev: str, timeout: float = 15.0) -> None:
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            subprocess.check_call(
                ["dd", f"if={dev}", "bs=512", "count=1", "iflag=direct", "status=none", "of=/dev/null"],
                stderr=subprocess.DEVNULL,
            )
            return
        except subprocess.CalledProcessError:
            time.sleep(0.5)
    raise SystemExit(f"device {dev} not ready")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--esp", default="http://192.168.178.88")
    ap.add_argument("--dev", default="/dev/sda")
    ap.add_argument("--warmup", type=int, default=8000)
    ap.add_argument("--sectors", type=int, default=192, help="4k-chunk burst size (sectors)")
    args = ap.parse_args()

    wait_dev(args.dev)
    st = get_status(args.esp)
    slot = st["msc"]["slotMap"][0]
    uid, lba0 = slot["uid"], slot["lba0"]
    print(f"fw={st['version']} uid={uid} lba0={lba0} ser={st['msc'].get('usbSerial')}")

    post(args.esp, "/api/lab/play", {"uid": uid})
    warm = False
    for i in range(60):
        st = get_status(args.esp)
        s = st["msc"]["stream"]
        print(
            f"t={i} active={s['active']} size={s['size']} id3={s['id3Len']} "
            f"underruns={s['underruns']} streamBytes={st['msc']['streamBytes']}"
        )
        if s["active"] and s["size"] >= args.warmup:
            warm = True
            break
        time.sleep(0.4)
    if not warm:
        raise SystemExit("stream not warm")

    before = get_status(args.esp)["msc"]
    print("burst…")
    t0 = time.time()
    dd_read(args.dev, lba0, args.sectors)
    dt = time.time() - t0
    after = get_status(args.esp)["msc"]
    sb = after["streamBytes"] - before["streamBytes"]
    ud = after["stream"]["underruns"] - before["stream"]["underruns"]
    print(f"dt={dt:.3f}s streamBytes+={sb} underruns+={ud} live≈{max(0, sb - ud)}")
    print("stream", after["stream"])

    time.sleep(1.5)
    mid = get_status(args.esp)["msc"]
    dd_read(args.dev, lba0 + args.sectors, args.sectors)
    end = get_status(args.esp)["msc"]
    print(
        f"2nd streamBytes+={end['streamBytes']-mid['streamBytes']} "
        f"underruns+={end['stream']['underruns']-mid['stream']['underruns']}"
    )


if __name__ == "__main__":
    main()
