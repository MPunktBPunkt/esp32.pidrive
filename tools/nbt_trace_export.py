#!/usr/bin/env python3
"""Best-effort export of diag/events → replay.json + helpers to emit synthetic patterns.

V1 cannot reconstruct full LBA bursts from 2026-09-28 artifacts (no complete MSC
trace ring). Use --synthetic for B1/Feld patterns; --diag for quiet/phase markers only.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path


def emit_synthetic(name: str, out: Path) -> None:
    # Delegate to existing library files if present
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
            "note": "Markers only — no LBA reads. Enrich with synthetic burst or larger ESP trace.",
        },
        "events": events_out,
    }
    out.write_text(json.dumps(doc, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {out} ({len(events_out)} markers; no SCSI reads)")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--synthetic", choices=("b1_burst_quiet", "feld_1001_guess_then_quiet", "sequential_past_head"))
    ap.add_argument("--diag", type=Path, help="path to pidrive_msc_diag.jsonl")
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()
    if args.synthetic:
        emit_synthetic(args.synthetic, args.out)
    elif args.diag:
        export_diag(args.diag, args.out)
    else:
        raise SystemExit("need --synthetic NAME or --diag PATH")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
