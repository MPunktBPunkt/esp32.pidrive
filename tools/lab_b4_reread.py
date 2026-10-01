#!/usr/bin/env python3
"""B4 SoftAP probe: head re-read after ring scroll must yield ID3 + live (not silence).

Requires warm stream (bridge → ESP) and labMode.
  python3 tools/lab_b4_reread.py --esp http://192.168.178.88
"""
from __future__ import annotations

import argparse
import json
import sys
import time
import urllib.request


def get_status(esp: str) -> dict:
    with urllib.request.urlopen(esp + "/api/status", timeout=5) as r:
        return json.load(r)


def overlay_read(esp: str, off: int, n: int) -> tuple[bytes, dict]:
    url = f"{esp}/api/lab/overlay_read?off={off}&n={n}"
    req = urllib.request.Request(url)
    with urllib.request.urlopen(req, timeout=10) as r:
        headers = {k.lower(): v for k, v in r.headers.items()}
        return r.read(), headers


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--esp", default="http://192.168.178.88")
    ap.add_argument("--wait-scroll", type=float, default=12.0, help="seconds for absBase>0")
    ap.add_argument("--n", type=int, default=4096)
    args = ap.parse_args()

    st = get_status(args.esp)
    s = (st.get("msc") or {}).get("stream") or {}
    print(
        f"fw={st.get('version')} active={s.get('active')} size={s.get('size')} "
        f"id3={s.get('id3Len')} absBase={s.get('absBase')} absEnd={s.get('absEnd')} "
        f"resyncs={s.get('headResyncs')}"
    )
    if not s.get("active") or int(s.get("size") or 0) < 8000:
        print("FAIL: stream not warm (start bridge + lab/play first)", file=sys.stderr)
        return 2

    body0, h0 = overlay_read(args.esp, 0, args.n)
    id3 = int(h0.get("x-stream-id3") or 0)
    print(
        f"read0 n={len(body0)} id3={id3} underrunΔ={h0.get('x-underruns-delta')} "
        f"resyncΔ={h0.get('x-head-resyncs-delta')} magic={body0[:3]!r}"
    )
    if id3 > 0 and body0[:3] != b"ID3":
        print("FAIL: expected sticky ID3 at file offset 0", file=sys.stderr)
        return 3

    # Wait until ring has scrolled (absBase > 0) so pre-B4 would serve silence on head re-read.
    t0 = time.time()
    scrolled = False
    while time.time() - t0 < args.wait_scroll:
        st = get_status(args.esp)
        s = (st.get("msc") or {}).get("stream") or {}
        ab = int(s.get("absBase") or 0)
        print(f"  wait absBase={ab} absEnd={s.get('absEnd')} size={s.get('size')}")
        if ab > 0:
            scrolled = True
            break
        time.sleep(0.8)
    if not scrolled:
        print("WARN: absBase still 0 — force extra pushes by waiting; B4 still checks ID3")

    st = get_status(args.esp)
    s = (st.get("msc") or {}).get("stream") or {}
    ud_before = int(s.get("underruns") or 0)
    rs_before = int(s.get("headResyncs") or 0)
    ab = int(s.get("absBase") or 0)

    body1, h1 = overlay_read(args.esp, 0, args.n)
    ud_delta = int(h1.get("x-underruns-delta") or 0)
    rs_delta = int(h1.get("x-head-resyncs-delta") or 0)
    print(
        f"read1(after scroll absBase={ab}) n={len(body1)} underrunΔ={ud_delta} "
        f"resyncΔ={rs_delta} magic={body1[:3]!r} "
        f"status underruns {ud_before}→{(get_status(args.esp).get('msc') or {}).get('stream', {}).get('underruns')} "
        f"resyncs {rs_before}→{(get_status(args.esp).get('msc') or {}).get('stream', {}).get('headResyncs')}"
    )

    ok = True
    if id3 > 0 and body1[:3] != b"ID3":
        print("FAIL: sticky ID3 lost after scroll", file=sys.stderr)
        ok = False
    if ab > 0 and rs_delta < 1:
        print("FAIL: expected headResync after scroll + head re-read", file=sys.stderr)
        ok = False
    # Live remapped audio should not underrun the entire payload
    audio_n = max(0, len(body1) - id3)
    if ab > 0 and audio_n > 0 and ud_delta >= audio_n:
        print(
            f"FAIL: underrunΔ={ud_delta} >= audio_n={audio_n} (still silence remap?)",
            file=sys.stderr,
        )
        ok = False
    if ok:
        print("PASS B4 SoftAP head re-read")
        return 0
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
