#!/usr/bin/env python3
"""Pi MSC-host lab test for esp32.pidrive 0.4.12+ (static FAT).

Proves Problem A without relying on Linux VFS dentry cache:
  - raw FAT + directory meta sectors identical before/during/after stream
  - slot LBA ranges from /api/status unchanged
  - payload head-read while stream active (streamBytes grows)

Usage on Pi (user in group ``disk``, or sudo for blockdev flush):
  python3 tools/msc_host_test.py --dev /dev/sda --esp http://192.168.178.89 --uid fav1

See: pidrive docs/betrieb/USB-MSC-STREAM-LISTING-2026-09-18.md
"""

from __future__ import annotations

import argparse
import json
import os
import struct
import subprocess
import sys
import time
import urllib.request
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

SECTOR = 512


@dataclass
class FatGeom:
    bps: int
    spc: int
    reserved: int
    fats: int
    root_ents: int
    sec_per_fat: int
    tot_sec: int
    fat_lba: int
    root_lba: int
    root_secs: int
    data_lba: int

    def cluster_to_lba(self, cl: int) -> int:
        if cl < 2:
            return self.data_lba
        return self.data_lba + (cl - 2) * self.spc


def read_lba_dd(dev: str, lba: int, count: int) -> bytes:
    """Raw sectors via dd iflag=direct (bypasses page cache)."""
    cmd = [
        "dd",
        f"if={dev}",
        "bs=512",
        f"skip={lba}",
        f"count={count}",
        "iflag=direct",
        "status=none",
    ]
    try:
        return subprocess.check_output(cmd, stderr=subprocess.DEVNULL)
    except subprocess.CalledProcessError:
        cmd = [
            "dd",
            f"if={dev}",
            "bs=512",
            f"skip={lba}",
            f"count={count}",
            "status=none",
        ]
        return subprocess.check_output(cmd, stderr=subprocess.DEVNULL)


def flush_bufs(dev: str) -> None:
    try:
        subprocess.run(["blockdev", "--flushbufs", dev], check=False, capture_output=True)
    except FileNotFoundError:
        pass


def parse_bpb(boot: bytes) -> FatGeom:
    if len(boot) < 64:
        raise SystemExit("boot sector too short")
    bps = struct.unpack_from("<H", boot, 11)[0]
    spc = boot[13]
    reserved = struct.unpack_from("<H", boot, 14)[0]
    fats = boot[16]
    root_ents = struct.unpack_from("<H", boot, 17)[0]
    tot16 = struct.unpack_from("<H", boot, 19)[0]
    spf = struct.unpack_from("<H", boot, 22)[0]
    tot32 = struct.unpack_from("<I", boot, 32)[0]
    tot = tot16 or tot32
    if bps != 512 or spc < 1 or fats < 1 or spf < 1:
        raise SystemExit(f"unexpected BPB: bps={bps} spc={spc} fats={fats} spf={spf}")
    fat_lba = reserved
    root_lba = reserved + fats * spf
    root_secs = (root_ents * 32 + bps - 1) // bps
    data_lba = root_lba + root_secs
    return FatGeom(bps, spc, reserved, fats, root_ents, spf, tot, fat_lba, root_lba, root_secs, data_lba)


def decode_lfn_chunk(ent: bytes) -> str:
    chars = []
    for a, b in ((1, 10), (14, 25), (28, 31)):
        chunk = ent[a : b + 1]
        for j in range(0, len(chunk) - 1, 2):
            c = chunk[j] | (chunk[j + 1] << 8)
            if c == 0 or c == 0xFFFF:
                return "".join(chars)
            chars.append(chr(c))
    return "".join(chars)


def parse_dir_sector(sec: bytes) -> list[dict]:
    out: list[dict] = []
    lfn_parts: list[tuple[int, str]] = []
    for i in range(0, len(sec), 32):
        e = sec[i : i + 32]
        if e[0] == 0x00:
            break
        if e[0] == 0xE5:
            lfn_parts = []
            continue
        attr = e[11]
        if attr == 0x0F:
            ord_ = e[0] & 0x3F
            lfn_parts.append((ord_, decode_lfn_chunk(e)))
            continue
        name83 = e[0:11]
        if name83[0:1] == b".":
            lfn_parts = []
            continue
        short = name83[0:8].decode("ascii", "replace").strip()
        ext = name83[8:11].decode("ascii", "replace").strip()
        short_name = f"{short}.{ext}" if ext else short
        if lfn_parts:
            lfn_parts.sort(key=lambda x: x[0])
            long_name = "".join(p[1] for p in lfn_parts)
        else:
            long_name = short_name
        lfn_parts = []
        cl_hi = struct.unpack_from("<H", e, 20)[0]
        cl_lo = struct.unpack_from("<H", e, 26)[0]
        cl = (cl_hi << 16) | cl_lo
        size = struct.unpack_from("<I", e, 28)[0]
        out.append(
            {
                "name": long_name,
                "short": short_name,
                "attr": attr,
                "cluster": cl,
                "size": size,
                "is_dir": bool(attr & 0x10),
            }
        )
    return out


def http_json(url: str, method: str = "GET", body: Optional[dict] = None, timeout: float = 5.0) -> dict:
    data = None
    headers = {}
    if body is not None:
        data = json.dumps(body).encode()
        headers["Content-Type"] = "application/json"
    req = urllib.request.Request(url, data=data, headers=headers, method=method)
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.loads(r.read().decode())


def status(esp: str) -> dict:
    return http_json(f"{esp.rstrip('/')}/api/status")


def lab_play(esp: str, uid: str) -> dict:
    return http_json(f"{esp.rstrip('/')}/api/lab/play", "POST", {"uid": uid})


def lab_stop(esp: str) -> dict:
    return http_json(f"{esp.rstrip('/')}/api/lab/stop", "POST", {})


def slot_map(st: dict) -> list[dict]:
    return (st.get("msc") or {}).get("slotMap") or []


def geometry_tuple(st: dict) -> tuple:
    return tuple((x.get("uid"), x.get("lba0"), x.get("lba1"), x.get("name")) for x in slot_map(st))


def main() -> int:
    ap = argparse.ArgumentParser(description="0.4.12 static-FAT MSC host test (Pi)")
    ap.add_argument("--dev", default="/dev/sda", help="block device (ESP MSC)")
    ap.add_argument("--esp", default="http://192.168.178.89", help="ESP base URL")
    ap.add_argument("--uid", default="fav1", help="station uid to stream")
    ap.add_argument("--wait", type=float, default=2.0, help="settle seconds after play/stop")
    ap.add_argument("--outdir", default="/tmp/msc_host_test", help="capture directory")
    args = ap.parse_args()

    dev = args.dev
    if not os.path.exists(dev):
        print(f"FAIL: device missing: {dev}", file=sys.stderr)
        return 2

    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)

    print("== msc_host_test 0.4.12 ==")
    print(f"dev={dev} esp={args.esp} uid={args.uid}")

    flush_bufs(dev)
    boot = read_lba_dd(dev, 0, 1)
    (outdir / "boot.bin").write_bytes(boot)
    geom = parse_bpb(boot)
    print(
        f"BPB: tot={geom.tot_sec} spc={geom.spc} fat_lba={geom.fat_lba}+{geom.fats}*{geom.sec_per_fat} "
        f"root_lba={geom.root_lba}({geom.root_secs}) data_lba={geom.data_lba}"
    )

    meta_start = geom.fat_lba
    # Only FAT + root + STATIONS/SETTINGS dir LBAs — NOT file payload (would change on stream).
    # data_lba=35 STATIONS, 39 SETTINGS; file data starts at cluster 4 / LBA 43.
    meta_count = geom.data_lba - geom.fat_lba + 8  # through ~LBA 42
    print(f"meta capture: LBA {meta_start}..{meta_start + meta_count - 1} ({meta_count} sectors)")

    results: dict[str, bool] = {}

    def snap(tag: str) -> bytes:
        flush_bufs(dev)
        time.sleep(0.15)
        flush_bufs(dev)
        blob = read_lba_dd(dev, meta_start, meta_count)
        (outdir / f"meta_{tag}.bin").write_bytes(blob)
        return blob

    try:
        st0 = status(args.esp)
    except Exception as e:
        print(f"FAIL: ESP status: {e}", file=sys.stderr)
        return 2

    ver = st0.get("version")
    fat_mode = (st0.get("msc") or {}).get("fatMode")
    print(f"ESP fw={ver} fatMode={fat_mode} plugged={(st0.get('msc') or {}).get('plugged')}")
    results["fw_0412"] = bool(ver and str(ver).startswith("0.4.12"))
    results["fat_mode_static"] = fat_mode == "static"

    try:
        lab_stop(args.esp)
    except Exception:
        pass
    time.sleep(0.5)
    st0 = status(args.esp)
    geo0 = geometry_tuple(st0)
    before = snap("before")

    stations_lba = geom.cluster_to_lba(2)
    flush_bufs(dev)
    st_dir = read_lba_dd(dev, stations_lba, 1)
    files = [e for e in parse_dir_sector(st_dir) if not e["is_dir"]]
    print(f"STATIONS@LBA{stations_lba}:")
    for e in files:
        print(f"  {e['name']!r}  size={e['size']}  cl={e['cluster']}")
    results["listing_before"] = len(files) >= 1
    sizes1 = sorted((e["name"], e["size"]) for e in files)
    names1 = sorted(e["name"] for e in files)

    print(f"\n-- stream start {args.uid} --")
    try:
        play = lab_play(args.esp, args.uid)
        print("lab/play:", play)
    except Exception as e:
        print(f"FAIL: lab/play: {e}", file=sys.stderr)
        return 2
    time.sleep(args.wait)

    # Ensure bridge has menu (fav*) — if lab play used unknown uid, still check geometry
    st1 = status(args.esp)
    m1 = st1.get("msc") or {}
    s1 = m1.get("stream") or {}
    geo1 = geometry_tuple(st1)
    print(
        f"during: streamSlot={m1.get('streamSlot')} active={s1.get('active')} "
        f"uid={s1.get('uid')} streamBytes={m1.get('streamBytes')}"
    )
    results["stream_active"] = bool(s1.get("active")) and int(m1.get("streamSlot", -1)) >= 0
    results["geometry_api_stable"] = geo0 == geo1
    if geo0 != geo1:
        print("API geometry BEFORE:", geo0)
        print("API geometry DURING:", geo1)

    during = snap("during")
    results["meta_sectors_identical"] = before == during
    if before != during:
        for i in range(0, min(len(before), len(during)), SECTOR):
            if before[i : i + SECTOR] != during[i : i + SECTOR]:
                print(f"DIFF first at relative sector {i // SECTOR} (abs LBA {meta_start + i // SECTOR})")
                break

    flush_bufs(dev)
    files2 = [e for e in parse_dir_sector(read_lba_dd(dev, stations_lba, 1)) if not e["is_dir"]]
    names2 = sorted(e["name"] for e in files2)
    sizes2 = sorted((e["name"], e["size"]) for e in files2)
    results["listing_during"] = len(files2) >= 1
    results["names_stable"] = names1 == names2
    results["sizes_stable"] = sizes1 == sizes2
    print(f"STATIONS during: {names2}")
    if sizes1 != sizes2:
        print("size BEFORE", sizes1)
        print("size DURING", sizes2)

    slot = None
    for x in slot_map(st1):
        if x.get("uid") == args.uid:
            slot = x
            break
    if slot is None:
        idx = m1.get("streamSlot")
        sm = slot_map(st1)
        if isinstance(idx, int) and 0 <= idx < len(sm):
            slot = sm[idx]

    if slot:
        lba0 = int(slot["lba0"])
        flush_bufs(dev)
        head = read_lba_dd(dev, lba0, 32)
        (outdir / "payload_head.bin").write_bytes(head)
        nonzero = sum(1 for b in head if b not in (0, 0xFF))
        sync = any(
            head[i] == 0xFF and (head[i + 1] & 0xE0) == 0xE0 for i in range(0, min(200, len(head) - 1))
        )
        sb0 = int(m1.get("streamBytes") or 0)
        time.sleep(0.8)
        st1b = status(args.esp)
        sb1 = int((st1b.get("msc") or {}).get("streamBytes") or 0)
        grew = sb1 > sb0
        print(f"payload LBA{lba0}: sync={sync} nonzero={nonzero} streamBytes {sb0}→{sb1} grew={grew}")
        results["payload_looks_mp3"] = sync and nonzero > 64
        results["stream_bytes_grew"] = grew
    else:
        print("WARN: no slot for payload read")
        results["payload_looks_mp3"] = False
        results["stream_bytes_grew"] = False

    print("\n-- stream stop --")
    try:
        lab_stop(args.esp)
    except Exception as e:
        print(f"WARN stop: {e}")
    time.sleep(args.wait)
    st2 = status(args.esp)
    geo2 = geometry_tuple(st2)
    after = snap("after")
    results["geometry_after_stable"] = geo0 == geo2
    results["meta_after_identical"] = before == after
    flush_bufs(dev)
    files3 = [e for e in parse_dir_sector(read_lba_dd(dev, stations_lba, 1)) if not e["is_dir"]]
    results["listing_after"] = len(files3) >= 1

    print("\n== RESULTS ==")
    order = [
        "fw_0412",
        "fat_mode_static",
        "listing_before",
        "stream_active",
        "geometry_api_stable",
        "meta_sectors_identical",
        "names_stable",
        "sizes_stable",
        "listing_during",
        "payload_looks_mp3",
        "stream_bytes_grew",
        "geometry_after_stable",
        "meta_after_identical",
        "listing_after",
    ]
    core = [
        "fat_mode_static",
        "stream_active",
        "geometry_api_stable",
        "meta_sectors_identical",
        "names_stable",
        "sizes_stable",
        "listing_during",
        "listing_after",
        "meta_after_identical",
    ]
    for k in order:
        ok = bool(results.get(k))
        mark = "PASS" if ok else "FAIL"
        star = " *" if k in core else ""
        print(f"  {mark}  {k}{star}")

    core_ok = all(results.get(k) for k in core)
    print()
    if core_ok:
        print("OVERALL: PASS — Problem A (static FAT / listing) OK on Pi host")
        rc = 0
    else:
        print("OVERALL: FAIL — core checks marked *")
        rc = 1

    report = {
        "version": ver,
        "fatMode": fat_mode,
        "uid": args.uid,
        "dev": dev,
        "results": results,
        "core_ok": core_ok,
        "stations_before": sizes1,
        "stations_during": sizes2,
        "geo_before": list(geo0),
        "geo_during": list(geo1),
    }
    (outdir / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"artifacts: {outdir}/")
    return rc


if __name__ == "__main__":
    sys.exit(main())
