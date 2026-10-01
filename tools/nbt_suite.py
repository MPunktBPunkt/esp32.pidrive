#!/usr/bin/env python3
"""Run NBT-replay scenarios 1–3 and write a dashboard under tools/reports/."""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from nbt_report import analyze, to_markdown  # noqa: E402

TRACES = ROOT / "traces"
REPORTS = ROOT / "reports"


SCENARIOS = [
    {
        "id": "burst_then_quiet",
        "trace": "b1_burst_quiet_gentle.replay.json",
        "need_stream": False,
        "settle_override": None,  # use CLI settle
    },
    {
        "id": "head_reread_after_quiet",
        "trace": "feld_1001_guess_then_quiet.replay.json",
        "need_stream": True,
        "extra_events": "head_reread",  # injected after quiet by suite
        "settle_override": 30,
    },
    {
        "id": "sequential_past_head",
        "trace": "sequential_past_head.replay.json",
        "need_stream": True,
        "settle_override": 5,
    },
]


def run_cmd(cmd: list[str]) -> int:
    print("+", " ".join(cmd), flush=True)
    return subprocess.call(cmd)


def lab_play(esp: str, uid: str = "fav0") -> None:
    import urllib.request

    req = urllib.request.Request(
        esp.rstrip("/") + "/api/lab/play",
        data=json.dumps({"uid": uid}).encode(),
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=10) as r:
            print("lab/play", r.read()[:200])
    except Exception as e:  # noqa: BLE001
        print("lab/play failed:", e)


def wait_warm(esp: str, timeout: float = 45.0) -> bool:
    import urllib.request

    t0 = time.time()
    while time.time() - t0 < timeout:
        with urllib.request.urlopen(esp.rstrip("/") + "/api/status", timeout=5) as r:
            st = json.load(r)
        s = st.get("stream") if isinstance(st.get("stream"), dict) else {}
        if not s:
            s = (st.get("msc") or {}).get("stream") or {}
        if s.get("active") and int(s.get("size") or 0) >= 8000:
            return True
        time.sleep(0.5)
    return False


def inject_head_reread(trace_path: Path, out_path: Path, settle_s: float) -> None:
    """After quiet, add slot-head reads (scenario 2)."""
    tr = json.loads(trace_path.read_text(encoding="utf-8"))
    geo = tr.get("geometry") or {}
    head = int(geo.get("slot1_lba") or geo.get("slot0_lba") or 57)
    events = list(tr.get("events") or [])
    for ev in events:
        if ev.get("op") == "quiet":
            ev["duration_ms"] = int(settle_s * 1000)
    t_last = max(int(e.get("t_ms") or 0) for e in events) if events else 0
    t = t_last + 20
    for j, off in enumerate((0, 8)):
        events.append({"t_ms": t + j * 150, "op": "read", "lba": head + off, "bytes": 4096})
    tr["events"] = events
    tr.setdefault("meta", {})["name"] = "head_reread_after_quiet"
    out_path.write_text(json.dumps(tr, indent=2) + "\n", encoding="utf-8")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--esp", default="http://192.168.178.88")
    ap.add_argument("--dev", default="/dev/sda")
    ap.add_argument("--sg", default="/dev/sg0")
    ap.add_argument("--mode", choices=("direct", "sg"), default="sg")
    ap.add_argument("--settle", type=float, default=30.0, help="default quiet seconds for scenario 1")
    ap.add_argument("--fw-tag", default="")
    args = ap.parse_args()

    REPORTS.mkdir(parents=True, exist_ok=True)
    ts = time.strftime("%Y%m%d-%H%M%S")
    fw = args.fw_tag
    rows = []

    for sc in SCENARIOS:
        sid = sc["id"]
        trace = TRACES / sc["trace"]
        if sc.get("extra_events") == "head_reread":
            trace = REPORTS / f"_tmp_{sid}.replay.json"
            inject_head_reread(
                TRACES / sc["trace"],
                trace,
                settle_s=float(sc.get("settle_override") or 30),
            )

        if sc.get("need_stream"):
            lab_play(args.esp, "fav0" if sid != "head_reread_after_quiet" else "fav1")
            time.sleep(2.0)
            if not wait_warm(args.esp, timeout=45.0):
                print(f"WARN: stream not warm for {sid} — continuing")

        out_raw = REPORTS / f"{sid}__{fw or 'fw'}__{ts}.json"
        cmd = [
            sys.executable,
            str(ROOT / "nbt_replay.py"),
            "--trace",
            str(trace),
            "--esp",
            args.esp,
            "--dev",
            args.dev,
            "--sg",
            args.sg,
            "--mode",
            args.mode,
            "--settle",
            str(args.settle if sc.get("settle_override") is None else sc["settle_override"]),
            "--out",
            str(out_raw),
        ]
        if sc.get("settle_override") is not None:
            cmd += ["--settle-override", str(sc["settle_override"])]

        rc = run_cmd(cmd)
        if not out_raw.exists():
            rows.append({"scenario": sid, "overall": "FAIL", "detail": f"replay rc={rc} no output"})
            continue
        run = json.loads(out_raw.read_text(encoding="utf-8"))
        if not fw:
            fw = (run.get("meta") or {}).get("fw_before") or ""
            out_raw2 = REPORTS / f"{sid}__{fw}__{ts}.json"
            if out_raw2 != out_raw:
                out_raw.rename(out_raw2)
                out_raw = out_raw2
                run = json.loads(out_raw.read_text(encoding="utf-8"))

        summary = analyze(run)
        md_path = out_raw.with_suffix(".md")
        md_path.write_text(to_markdown(summary, run), encoding="utf-8")
        summary_path = Path(str(out_raw).replace(".json", ".summary.json"))
        summary_path.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
        rows.append(
            {
                "scenario": sid,
                "overall": summary["overall"],
                "esp_reboot": summary["esp_reboot"],
                "underrun_delta": summary["underrun_delta"],
                "live_ratio": summary["live_ratio"],
                "read_bytes": summary["read_bytes_total"],
                "report": str(md_path.name),
            }
        )

    dash = REPORTS / f"dashboard__{fw or 'fw'}__{ts}.md"
    lines = [
        f"# NBT-Replay Suite Dashboard — {fw} — {ts}",
        "",
        f"Mode: `{args.mode}` · ESP: `{args.esp}`",
        "",
        "| Scenario | Overall | reboot | underrunΔ | live_ratio | read_bytes | report |",
        "|----------|---------|--------|-----------|------------|------------|--------|",
    ]
    for r in rows:
        lr = r["live_ratio"]
        lr_s = f"{lr:.3f}" if isinstance(lr, float) else str(lr)
        lines.append(
            f"| `{r['scenario']}` | **{r['overall']}** | {r['esp_reboot']} | "
            f"{r['underrun_delta']} | {lr_s} | {r['read_bytes']} | {r['report']} |"
        )
    lines.append("")
    dash.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(dash.read_text())
    print(f"dashboard: {dash}")
    return 0 if all(r["overall"] != "FAIL" for r in rows) else 1


if __name__ == "__main__":
    raise SystemExit(main())
