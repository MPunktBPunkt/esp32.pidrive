#!/usr/bin/env python3
"""Timing-faithful MSC host replay of NBT-like read traces against esp32.pidrive.

  python3 tools/nbt_replay.py --trace tools/traces/b1_burst_quiet.replay.json \\
    --esp http://192.168.178.88 --sg /dev/sg0 --mode sg --settle 60

Do NOT mount the gadget. Prefer --mode sg (SG_IO) to avoid usb-storage burst resets.
"""
from __future__ import annotations

import argparse
import json
import os
import struct
import sys
import threading
import time
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any


SECTOR = 512
SG_IO = 0x2285  # Linux <scsi/sg.h>


def http_json(url: str, timeout: float = 3.0) -> dict[str, Any]:
    with urllib.request.urlopen(url, timeout=timeout) as r:
        return json.load(r)


def esp_status(esp: str) -> dict[str, Any]:
    return http_json(esp.rstrip("/") + "/api/status")


def device_mounted(dev: str) -> bool:
    try:
        with open("/proc/mounts", encoding="utf-8") as f:
            for line in f:
                if line.split()[0] == dev:
                    return True
    except OSError:
        pass
    return False


def align_buf(n: int):
    import ctypes

    cbuf = (ctypes.c_char * (n + 4096))()
    base = ctypes.addressof(cbuf)
    off = (4096 - (base % 4096)) % 4096
    return memoryview(cbuf).cast("B")[off : off + n]


def open_direct(dev: str) -> int:
    flags = os.O_RDONLY | getattr(os, "O_DIRECT", 0)
    return os.open(dev, flags)


def read_direct(fd: int, lba: int, nbytes: int) -> bytes:
    if nbytes % SECTOR:
        raise ValueError(f"O_DIRECT size must be sector-multiple, got {nbytes}")
    buf = align_buf(nbytes)
    got = os.pread(fd, buf, nbytes, lba * SECTOR)
    return bytes(buf[:got])


def read_sg(sg_fd: int, lba: int, nbytes: int) -> bytes:
    import ctypes
    import fcntl

    if nbytes % SECTOR:
        raise ValueError(f"SG size must be sector-multiple, got {nbytes}")
    nsec = nbytes // SECTOR
    cmd = struct.pack(">BBIBHB", 0x28, 0, lba & 0xFFFFFFFF, 0, nsec & 0xFFFF, 0)

    class SgIoHdr(ctypes.Structure):
        _fields_ = [
            ("interface_id", ctypes.c_int),
            ("dxfer_direction", ctypes.c_int),
            ("cmd_len", ctypes.c_ubyte),
            ("mx_sb_len", ctypes.c_ubyte),
            ("iovec_count", ctypes.c_ushort),
            ("dxfer_len", ctypes.c_uint),
            ("dxferp", ctypes.c_void_p),
            ("cmdp", ctypes.c_void_p),
            ("sbp", ctypes.c_void_p),
            ("timeout", ctypes.c_uint),
            ("flags", ctypes.c_uint),
            ("pack_id", ctypes.c_int),
            ("usr_ptr", ctypes.c_void_p),
            ("status", ctypes.c_ubyte),
            ("masked_status", ctypes.c_ubyte),
            ("msg_status", ctypes.c_ubyte),
            ("sb_len_wr", ctypes.c_ubyte),
            ("host_status", ctypes.c_ushort),
            ("driver_status", ctypes.c_ushort),
            ("resid", ctypes.c_int),
            ("duration", ctypes.c_uint),
            ("info", ctypes.c_uint),
        ]

    SG_DXFER_FROM_DEV = -3
    dxfer_arr = (ctypes.c_ubyte * nbytes)()
    cmd_buf = (ctypes.c_ubyte * len(cmd)).from_buffer_copy(cmd)
    sb_arr = (ctypes.c_ubyte * 32)()
    hdr = SgIoHdr()
    hdr.interface_id = ord("S")
    hdr.dxfer_direction = SG_DXFER_FROM_DEV
    hdr.cmd_len = len(cmd)
    hdr.mx_sb_len = 32
    hdr.dxfer_len = nbytes
    hdr.dxferp = ctypes.addressof(dxfer_arr)
    hdr.cmdp = ctypes.addressof(cmd_buf)
    hdr.sbp = ctypes.addressof(sb_arr)
    hdr.timeout = 5000
    fcntl.ioctl(sg_fd, SG_IO, hdr)
    if hdr.status not in (0,):
        raise OSError(
            f"SG_IO status={hdr.status} host={hdr.host_status} "
            f"driver={hdr.driver_status} resid={hdr.resid}"
        )
    return bytes(dxfer_arr)


class Sampler:
    def __init__(self, esp: str, interval_s: float = 1.0) -> None:
        self.esp = esp
        self.interval_s = interval_s
        self.samples: list[dict[str, Any]] = []
        self._stop = threading.Event()
        self._thr: threading.Thread | None = None
        self._lock = threading.Lock()

    def start(self) -> None:
        self._thr = threading.Thread(target=self._loop, name="esp-sampler", daemon=True)
        self._thr.start()

    def stop(self) -> None:
        self._stop.set()
        if self._thr:
            self._thr.join(timeout=3)

    def mark(self, label: str, **extra: Any) -> None:
        snap = self._snap(label=label, **extra)
        with self._lock:
            self.samples.append(snap)

    def _snap(self, label: str = "poll", **extra: Any) -> dict[str, Any]:
        row: dict[str, Any] = {"t_wall": time.time(), "label": label}
        row.update(extra)
        try:
            st = esp_status(self.esp)
            m = st.get("msc") or {}
            s = m.get("stream") or {}
            row.update(
                {
                    "version": st.get("version"),
                    "uptime": st.get("uptime"),
                    "pumpTcpUp": st.get("pumpTcpUp"),
                    "phase": m.get("phase"),
                    "lastReadLba": m.get("lastReadLba"),
                    "streamBytes": m.get("streamBytes"),
                    "playGuessCount": m.get("playGuessCount"),
                    "usbSerial": m.get("usbSerial"),
                    "remountGen": m.get("remountGen"),
                    "stream_active": s.get("active"),
                    "underruns": s.get("underruns"),
                    "absBase": s.get("absBase"),
                    "absEnd": s.get("absEnd"),
                    "headResyncs": s.get("headResyncs"),
                    "cursorArmed": s.get("cursorArmed"),
                    "hostAbsCursor": s.get("hostAbsCursor"),
                    "id3Len": s.get("id3Len"),
                    "stream_size": s.get("size"),
                }
            )
        except Exception as e:  # noqa: BLE001
            row["error"] = str(e)[:160]
        return row

    def _loop(self) -> None:
        while not self._stop.wait(self.interval_s):
            snap = self._snap()
            with self._lock:
                self.samples.append(snap)


def load_trace(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def run_replay(args: argparse.Namespace) -> dict[str, Any]:
    trace = load_trace(Path(args.trace))
    events = list(trace.get("events") or [])
    if not events:
        raise SystemExit("trace has no events")

    mode = args.mode
    if mode == "sg":
        if not args.sg:
            raise SystemExit("--sg /dev/sgN required for --mode sg")
        if device_mounted(args.dev):
            print(f"WARN: {args.dev} is mounted — unmount before SG/MSC replay", file=sys.stderr)
        fd = os.open(args.sg, os.O_RDWR)
        reader = lambda lba, n: read_sg(fd, lba, n)  # noqa: E731
        transport = f"sg:{args.sg}"
    else:
        if not args.dev:
            raise SystemExit("--dev /dev/sdX required for --mode direct")
        if device_mounted(args.dev):
            raise SystemExit(f"{args.dev} is mounted — unmount first")
        fd = open_direct(args.dev)
        reader = lambda lba, n: read_direct(fd, lba, n)  # noqa: E731
        transport = f"direct:{args.dev}"

    sampler = Sampler(args.esp, interval_s=args.sample)
    t0 = time.time()
    sampler.start()
    sampler.mark("start", transport=transport, trace=trace.get("meta", {}).get("name"))

    try:
        st0 = esp_status(args.esp)
    except Exception as e:  # noqa: BLE001
        st0 = {"error": str(e)}

    read_log: list[dict[str, Any]] = []
    quiet_total_ms = 0
    errors: list[str] = []

    def sleep_until(t_ms: int) -> None:
        target = t0 + t_ms / 1000.0
        delay = target - time.time()
        if delay > 0:
            time.sleep(delay)

    try:
        for ev in events:
            op = ev.get("op")
            t_ms = int(ev.get("t_ms") or 0)
            sleep_until(t_ms)
            if op == "read":
                lba = int(ev["lba"])
                nbytes = int(ev.get("bytes") or SECTOR)
                sampler.mark("before_read", t_ms=t_ms, lba=lba, bytes=nbytes)
                err = None
                try:
                    data = reader(lba, nbytes)
                    n = len(data)
                except Exception as e:  # noqa: BLE001
                    err = str(e)[:200]
                    n = 0
                    errors.append(f"t={t_ms} lba={lba}: {err}")
                    data = b""
                read_log.append(
                    {
                        "t_ms": t_ms,
                        "lba": lba,
                        "bytes": nbytes,
                        "got": n,
                        "error": err,
                        "head4": list(data[:4]) if data else [],
                    }
                )
                sampler.mark("after_read", t_ms=t_ms, lba=lba, got=n, error=err)
            elif op == "quiet":
                dur = int(ev.get("duration_ms") or args.settle * 1000)
                # allow CLI settle to extend/override quiet for lab speed
                if args.settle_override is not None:
                    dur = int(args.settle_override * 1000)
                quiet_total_ms += dur
                sampler.mark("quiet_begin", t_ms=t_ms, duration_ms=dur)
                time.sleep(dur / 1000.0)
                sampler.mark("quiet_end", t_ms=t_ms)
            elif op == "marker":
                sampler.mark("marker", t_ms=t_ms, note=ev.get("note"))
            else:
                errors.append(f"unknown op {op}")

        # trailing settle if last event wasn't quiet long enough
        if args.settle > 0 and quiet_total_ms < args.settle * 1000:
            extra = args.settle * 1000 - quiet_total_ms
            sampler.mark("settle_extra_begin", duration_ms=extra)
            time.sleep(extra / 1000.0)
            sampler.mark("settle_extra_end")
    finally:
        sampler.stop()
        os.close(fd)

    try:
        st1 = esp_status(args.esp)
    except Exception as e:  # noqa: BLE001
        st1 = {"error": str(e)}

    result = {
        "meta": {
            "trace": trace.get("meta"),
            "transport": transport,
            "esp": args.esp,
            "started": t0,
            "ended": time.time(),
            "fw_before": (st0.get("version") if isinstance(st0, dict) else None),
            "fw_after": (st1.get("version") if isinstance(st1, dict) else None),
        },
        "trace_geometry": trace.get("geometry"),
        "reads": read_log,
        "errors": errors,
        "status_before": st0,
        "status_after": st1,
        "samples": sampler.samples,
    }
    return result


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--trace", required=True)
    ap.add_argument("--esp", default="http://192.168.178.88")
    ap.add_argument("--dev", default="/dev/sda")
    ap.add_argument("--sg", default="/dev/sg0")
    ap.add_argument("--mode", choices=("direct", "sg"), default="sg")
    ap.add_argument("--settle", type=float, default=60.0, help="min quiet/settle seconds after run")
    ap.add_argument(
        "--settle-override",
        type=float,
        default=None,
        help="replace quiet duration_ms in trace (lab speed)",
    )
    ap.add_argument("--sample", type=float, default=1.0)
    ap.add_argument("--out", default="")
    args = ap.parse_args()

    result = run_replay(args)
    text = json.dumps(result, indent=2, ensure_ascii=False)
    if args.out:
        Path(args.out).parent.mkdir(parents=True, exist_ok=True)
        Path(args.out).write_text(text + "\n", encoding="utf-8")
        print(f"wrote {args.out}")
    else:
        print(text)
    n_err = len(result.get("errors") or [])
    print(
        f"reads={len(result['reads'])} errors={n_err} "
        f"fw={result['meta'].get('fw_before')}→{result['meta'].get('fw_after')} "
        f"transport={result['meta'].get('transport')}"
    )
    return 1 if n_err else 0


if __name__ == "__main__":
    raise SystemExit(main())
