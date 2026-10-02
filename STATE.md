# STATE — esp32.pidrive

Lebender Projektstand. Kurz halten; Details in `docs/planung/` und Feldbericht.

| Feld | Wert |
|------|------|
| Stand | 2026-10-02 |
| Phase | **Firmware 0.4.36-dev** — Feld §11.10 Mount-Scan+Cache; nächster Fokus **B7/150s** (kein separates Probe-FW) |
| Repo | `MPunktBPunkt/esp32.pidrive` |
| Build | PlatformIO `env:pidrive-s3` (`pio run`) |
| Dist | `dist/pidrive.0.4.36-dev.ota.esp32s3.bin` (nach Build) |
| Hub-Depot | `iobroker.esp-hub/firmware/pidrive.0.4.36-dev.*.esp32s3.bin` |
| Hardware | ESP32-S3-DevKitC-1 (OTG + UART) |
| Pi-Link | [PUMP.md](docs/planung/PUMP.md) · Bridge `tools/pump_bridge.py` |
| Feldbericht | [FELDTEST-ESP-MSC-BMW-2026-09-28](https://github.com/MPunktBPunkt/pidrive/blob/main/docs/betrieb/FELDTEST-ESP-MSC-BMW-2026-09-28.md) |
| Auftrag | [AUFTRAG-MSC-READS-STREAMING](docs/auftraege/AUFTRAG-MSC-READS-STREAMING.md) |

## Aktueller Fokus

1. **Kein neues Probe-FW-Repo** — Lab-APIs + `nbt_*` Harness reichen; BMW-Nachlesen = Auto (pidrive B7).
2. Zählersemantik dokumentiert (`streamBytes` = Live-Overlay-Serve; `readOverflow` = Telemetrie-Queue).
3. Ring/Präfill **nach** B7-Feldpass, nicht vorher.

## Letzte Änderung

- 2026-10-02: Feld §11.10 Präzisierung; Entscheidung gegen Probe-FW-Fork; Fokus B7
- 2026-10-01 Abend: **B6-Automation** in `nbt_report`/`nbt_suite` — Verdict-IDs `pre_warm_bytes`, `stream_after_arm`, `overlay_live` (KPI-Feld `live_ratio`); Suite-Lauf 0.4.36: preΔ=0 PASS, streamΔ=180224 PASS, live_ratio≈0.29 WARN
- 2026-10-01 Abend: **0.4.36-dev** `kOverlayWarmupBytes=0` + arm on `audio_start` (B6 Hebel A)
- 2026-10-01 Abend: **t_ms-Export-Fix**; Suite `prefetch_then_warm`; Feld-Traces
- 2026-10-01 Abend: **0.4.35-dev** play_uid-Replay
