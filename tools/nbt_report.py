#!/usr/bin/env python3
"""Evaluate an nbt_replay.py raw JSON run → markdown + summary JSON."""
from __future__ import annotations

import argparse
import json
import re
from pathlib import Path
from typing import Any


def parse_uptime_s(up: Any) -> float | None:
    if up is None:
        return None
    if isinstance(up, (int, float)):
        return float(up)
    s = str(up)
    total = 0.0
    m = re.search(r"(\d+)\s*h", s)
    if m:
        total += int(m.group(1)) * 3600
    m = re.search(r"(\d+)\s*min", s)
    if m:
        total += int(m.group(1)) * 60
    m = re.search(r"(\d+)\s*s", s)
    if m:
        total += int(m.group(1))
    if total == 0 and s.isdigit():
        return float(s)
    return total if ("h" in s or "min" in s or "s" in s or total) else None


def stream_of(st: dict[str, Any]) -> dict[str, Any]:
    """Prefer top-level /api/status `stream`; fall back to legacy `msc.stream`."""
    st = st or {}
    s = st.get("stream")
    if isinstance(s, dict) and s:
        return s
    return ((st.get("msc") or {}).get("stream") or {})

def analyze(run: dict[str, Any]) -> dict[str, Any]:
    reads = run.get("reads") or []
    samples = run.get("samples") or []
    st0 = run.get("status_before") or {}
    st1 = run.get("status_after") or {}
    s0, s1 = stream_of(st0), stream_of(st1)

    read_bytes = sum(int(r.get("got") or 0) for r in reads)
    t_first = reads[0]["t_ms"] if reads else None
    t_last = reads[-1]["t_ms"] if reads else None
    gaps = []
    for a, b in zip(reads, reads[1:]):
        gaps.append(int(b["t_ms"]) - int(a["t_ms"]))
    max_gap = max(gaps) if gaps else 0

    ud0 = int(s0.get("underruns") or 0)
    ud1 = int(s1.get("underruns") or 0)
    sb0 = int((st0.get("msc") or {}).get("streamBytes") or 0)
    sb1 = int((st1.get("msc") or {}).get("streamBytes") or 0)
    pre0 = int((st0.get("msc") or {}).get("preWarmHostBytes") or 0)
    pre1 = int((st1.get("msc") or {}).get("preWarmHostBytes") or 0)
    # Prefer sampler deltas across before_read/after_read if available
    read_marks = [x for x in samples if x.get("label") in ("before_read", "after_read", "start", "quiet_begin")]
    if samples:
        # underrun delta over whole run from first/last good sample
        good = [x for x in samples if "underruns" in x]
        if good:
            ud0 = int(good[0].get("underruns") or ud0)
            ud1 = int(good[-1].get("underruns") or ud1)
            sb0 = int(good[0].get("streamBytes") or sb0)
            sb1 = int(good[-1].get("streamBytes") or sb1)
        good_pre = [x for x in samples if "preWarmHostBytes" in x]
        if good_pre:
            pre0 = int(good_pre[0].get("preWarmHostBytes") or pre0)
            pre1 = int(good_pre[-1].get("preWarmHostBytes") or pre1)

    ud_delta = ud1 - ud0
    sb_delta = sb1 - sb0
    pre_delta = pre1 - pre0
    if ud_delta < 0:
        ud_delta = 0  # stream restart
    if sb_delta < 0:
        sb_delta = 0
    if pre_delta < 0:
        pre_delta = 0
    live = max(0, sb_delta - max(0, ud_delta))
    live_ratio = (live / sb_delta) if sb_delta > 0 else None

    hr0 = int(s0.get("headResyncs") or 0)
    hr1 = int(s1.get("headResyncs") or 0)
    if samples:
        good = [x for x in samples if "headResyncs" in x]
        if good:
            hr0 = int(good[0].get("headResyncs") or 0)
            hr1 = int(good[-1].get("headResyncs") or 0)

    up0 = parse_uptime_s(st0.get("uptime"))
    up1 = parse_uptime_s(st1.get("uptime"))
    # also scan samples for uptime regression
    reboot = False
    if up0 is not None and up1 is not None and up1 + 5 < up0:
        reboot = True
    ups = [parse_uptime_s(x.get("uptime")) for x in samples]
    ups = [u for u in ups if u is not None]
    for a, b in zip(ups, ups[1:]):
        if b + 5 < a:
            reboot = True
            break

    ver0 = (run.get("meta") or {}).get("fw_before") or st0.get("version")
    ver1 = (run.get("meta") or {}).get("fw_after") or st1.get("version")
    if ver0 and ver1 and ver0 != ver1:
        reboot = True

    verdicts: list[dict[str, str]] = []
    if reboot:
        verdicts.append({"id": "esp_reboot", "result": "FAIL", "detail": f"uptime/fw {ver0}/{up0}→{ver1}/{up1}"})
    else:
        verdicts.append({"id": "esp_reboot", "result": "PASS", "detail": "no uptime regression"})

    # B6: warmup=0 → silence window must not inflate preWarmHostBytes.
    if pre_delta <= 4096:
        verdicts.append(
            {"id": "pre_warm_bytes", "result": "PASS", "detail": f"preΔ={pre_delta} (target 0 @ warmup=0)"}
        )
    elif pre_delta < 65536:
        verdicts.append(
            {"id": "pre_warm_bytes", "result": "WARN", "detail": f"preΔ={pre_delta} (partial silence window)"}
        )
    else:
        verdicts.append(
            {
                "id": "pre_warm_bytes",
                "result": "FAIL",
                "detail": f"preΔ={pre_delta} (prefetch still on silence geometry)",
            }
        )

    # B6: after arm, prefetch-scale slot reads must hit the live overlay path.
    # Only assert when the trace actually issued a large file-slot consume (≥64 KiB);
    # quiet/head-reread scenarios stay N/A so they don't false-FAIL.
    if read_bytes >= 65536:
        if sb_delta >= 65536:
            verdicts.append(
                {
                    "id": "stream_after_arm",
                    "result": "PASS",
                    "detail": f"streamBytesΔ={sb_delta} (live path served prefetch)",
                }
            )
        elif sb_delta > 0:
            verdicts.append(
                {
                    "id": "stream_after_arm",
                    "result": "WARN",
                    "detail": f"streamBytesΔ={sb_delta} (small; expect ~180 KiB prefetch)",
                }
            )
        else:
            verdicts.append(
                {
                    "id": "stream_after_arm",
                    "result": "FAIL",
                    "detail": "streamBytesΔ=0 (prefetch missed live path / not armed)",
                }
            )
    else:
        verdicts.append(
            {
                "id": "stream_after_arm",
                "result": "PASS",
                "detail": f"n/a (read_bytes={read_bytes} < 64 KiB; not a prefetch assert)",
            }
        )

    # live_ratio = (streamBytes − underruns) / streamBytes — Pi ring-prefill KPI.
    # Path proven (sbΔ>0) but underrun-dominated → WARN (prefill pending), not FAIL.
    if sb_delta > 0 and live_ratio is not None:
        if live_ratio >= 0.9 and ud_delta <= sb_delta * 0.1:
            verdicts.append({"id": "overlay_live", "result": "PASS", "detail": f"live_ratio={live_ratio:.3f}"})
        elif live_ratio >= 0.5:
            verdicts.append({"id": "overlay_live", "result": "WARN", "detail": f"live_ratio={live_ratio:.3f}"})
        else:
            verdicts.append(
                {
                    "id": "overlay_live",
                    "result": "WARN",
                    "detail": f"live_ratio={live_ratio:.3f} (underrun silence; ring prefill next)",
                }
            )
    else:
        verdicts.append(
            {
                "id": "overlay_live",
                "result": "WARN",
                "detail": "no streamBytes growth (overlay idle or silence-only / no bridge)",
            }
        )

    if hr1 > hr0:
        verdicts.append({"id": "head_resync", "result": "PASS", "detail": f"headResyncs {hr0}→{hr1}"})
    else:
        verdicts.append({"id": "head_resync", "result": "WARN", "detail": f"headResyncs {hr0}→{hr1}"})

    errors = run.get("errors") or []
    if errors:
        verdicts.append({"id": "io_errors", "result": "FAIL", "detail": f"{len(errors)} IO errors"})
    else:
        verdicts.append({"id": "io_errors", "result": "PASS", "detail": "0 IO errors"})

    overall = "PASS"
    if any(v["result"] == "FAIL" for v in verdicts):
        overall = "FAIL"
    elif any(v["result"] == "WARN" for v in verdicts):
        overall = "WARN"

    return {
        "overall": overall,
        "read_bytes_total": read_bytes,
        "read_count": len(reads),
        "first_read_ms": t_first,
        "last_read_ms": t_last,
        "max_read_gap_ms": max_gap,
        "underrun_delta": ud_delta,
        "stream_bytes_delta": sb_delta,
        "pre_warm_delta": pre_delta,
        "live_ratio": live_ratio,
        "head_resync_delta": hr1 - hr0,
        "esp_reboot": reboot,
        "fw_before": ver0,
        "fw_after": ver1,
        "verdicts": verdicts,
        "transport": (run.get("meta") or {}).get("transport"),
        "trace": ((run.get("meta") or {}).get("trace") or {}).get("name"),
    }


def to_markdown(summary: dict[str, Any], run: dict[str, Any]) -> str:
    lines = [
        f"# NBT-Replay Report — {summary.get('trace') or '?'}",
        "",
        f"**Overall:** {summary['overall']}  ",
        f"**Transport:** `{summary.get('transport')}`  ",
        f"**FW:** {summary.get('fw_before')} → {summary.get('fw_after')}  ",
        "",
        "## Kennzahlen",
        "",
        "| Key | Value |",
        "|-----|-------|",
    ]
    for k in (
        "read_count",
        "read_bytes_total",
        "first_read_ms",
        "last_read_ms",
        "max_read_gap_ms",
        "underrun_delta",
        "stream_bytes_delta",
        "pre_warm_delta",
        "live_ratio",
        "head_resync_delta",
        "esp_reboot",
    ):
        v = summary.get(k)
        if isinstance(v, float):
            v = f"{v:.3f}"
        lines.append(f"| `{k}` | {v} |")
    lines += ["", "## Verdicts", ""]
    for v in summary.get("verdicts") or []:
        lines.append(f"- **{v['result']}** `{v['id']}` — {v['detail']}")
    errs = run.get("errors") or []
    if errs:
        lines += ["", "## IO errors", ""]
        for e in errs[:20]:
            lines.append(f"- {e}")
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("run_json")
    ap.add_argument("--out-md", default="")
    ap.add_argument("--out-json", default="")
    args = ap.parse_args()
    run = json.loads(Path(args.run_json).read_text(encoding="utf-8"))
    summary = analyze(run)
    md = to_markdown(summary, run)
    print(md)
    if args.out_md:
        Path(args.out_md).write_text(md, encoding="utf-8")
    if args.out_json:
        Path(args.out_json).write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    return 0 if summary["overall"] != "FAIL" else 1


if __name__ == "__main__":
    raise SystemExit(main())
