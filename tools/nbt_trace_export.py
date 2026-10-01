#!/usr/bin/env python3
"""Best-effort export of diag/events/msc.reads → replay.json + synthetic helpers.

- --reads: expand batched msc.reads jsonl into timed 4KiB (or chunk) read events
- --diag: quiet/phase markers only (legacy)
- --synthetic: copy known library traces
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path


def emit_synthetic(name: str, out: Path) -> None:
    lib = Path(__file__).resolve().parent / "traces" / f"{name}.replay.json"
    if lib.exists():
        out.write_text(lib.read_text(encoding="utf-8"), encoding="utf-8")
        print(f"copied {lib} → {out}")
        return
    raise SystemExit(f"unknown synthetic {name}; known under tools/traces/")


def export_diag(diag_path: Path, out: Path) -> None:
    events_out = []
    t0 = None
    with diag_path.open(encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                row = json.loads(line)
            except json.JSONDecodeError:
                continue
            ts = float(row.get("ts") or 0)
            if t0 is None:
                t0 = ts
            t_ms = int((ts - t0) * 1000)
            code = row.get("code") or ""
            detail = row.get("detail") or ""
            if code in ("msc.quiet", "msc.phase") and "quiet" in f"{code} {detail}":
                events_out.append(
                    {
                        "t_ms": t_ms,
                        "op": "quiet",
                        "duration_ms": 2000,
                        "note": f"{code} {detail}",
                    }
                )
            elif code.startswith("play.") or code.startswith("msc."):
                events_out.append(
                    {
                        "t_ms": t_ms,
                        "op": "marker",
                        "note": f"{code} {detail}",
                    }
                )
    doc = {
        "meta": {
            "name": diag_path.stem,
            "source": "diag-export-partial",
            "note": "Markers only — no LBA reads. Prefer --reads for msc.reads jsonl.",
        },
        "events": events_out,
    }
    out.write_text(json.dumps(doc, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {out} ({len(events_out)} markers; no SCSI reads)")


def export_reads(reads_path: Path, out: Path, chunk: int = 4096) -> None:
    """Expand burst rows into per-chunk read events for nbt_replay."""
    events_out: list[dict] = []
    t0_wall = None
    ms0_esp = None
    overflow_max = 0
    with reads_path.open(encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                row = json.loads(line)
            except json.JSONDecodeError:
                continue
            ts = float(row.get("ts") or 0)
            if t0_wall is None:
                t0_wall = ts
            esp_ms = int(row.get("ms") or 0)
            if ms0_esp is None and esp_ms:
                ms0_esp = esp_ms
            # Prefer ESP millis relative to first sample; fall back to wall clock
            if ms0_esp is not None and esp_ms:
                t_ms = max(0, esp_ms - ms0_esp)
            else:
                t_ms = int((ts - (t0_wall or ts)) * 1000)

            n = int(row.get("n") or 0)
            total = int(row.get("bytes") or 0)
            lba0 = int(row.get("lba0") or 0)
            lba1 = int(row.get("lba1") or lba0)
            gap = int(row.get("gap") or 0)
            ov = int(row.get("ov") or 0)
            overflow_max = max(overflow_max, ov)
            kind = int(row.get("kind") or 0)
            if n <= 0 or total <= 0:
                continue

            # Spread reads across [t_ms, …] with gap between chunks when n>1
            per = max(512, total // n) if n else chunk
            # Prefer sector-aligned 4KiB when it divides
            if per % 512:
                per = chunk
            step_lba = max(1, per // 512)
            # If lba span known, distribute
            span = max(0, lba1 - lba0)
            for i in range(n):
                if n > 1 and span > 0:
                    lba = lba0 + (span * i) // max(n - 1, 1)
                else:
                    lba = lba0 + i * step_lba
                ev_t = t_ms + (gap if i == 0 else 0) + i * max(gap, 1)
                events_out.append(
                    {
                        "t_ms": ev_t,
                        "op": "read",
                        "lba": lba,
                        "bytes": per if i < n - 1 else max(per, total - per * (n - 1)),
                        "kind": kind,
                    }
                )

    # Collapse trailing quiet: if last activity then long gap to end of file — skip
    doc = {
        "meta": {
            "name": reads_path.stem,
            "source": "msc.reads-jsonl",
            "note": f"Expanded bursts; overflow_max={overflow_max}. Gaps approximated.",
            "overflow_max": overflow_max,
        },
        "geometry": {"sector": 512},
        "events": events_out,
    }
    out.write_text(json.dumps(doc, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {out} ({len(events_out)} reads from {reads_path})")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--synthetic",
        choices=(
            "b1_burst_quiet",
            "b1_burst_quiet_gentle",
            "feld_1001_guess_then_quiet",
            "feld_heimabend_6s_cache",
            "sequential_past_head",
        ),
    )
    ap.add_argument("--diag", type=Path, help="path to pidrive_msc_diag.jsonl")
    ap.add_argument("--reads", type=Path, help="path to pidrive_msc_reads.jsonl")
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()
    if args.synthetic:
        emit_synthetic(args.synthetic, args.out)
    elif args.reads:
        export_reads(args.reads, args.out)
    elif args.diag:
        export_diag(args.diag, args.out)
    else:
        raise SystemExit("need --synthetic NAME, --reads PATH, or --diag PATH")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
