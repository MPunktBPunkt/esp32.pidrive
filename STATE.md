# STATE — esp32.pidrive

Lebender Projektstand. Kurz halten; Details in `docs/planung/` und Feldbericht.

| Feld | Wert |
|------|------|
| Stand | 2026-10-01 |
| Phase | **Firmware 0.4.32-dev** — `msc.reads` PUMP-Streaming (Burst-Aggregation) |
| Repo | `MPunktBPunkt/esp32.pidrive` |
| Build | PlatformIO `env:pidrive-s3` (`pio run`) |
| Dist | `dist/pidrive.0.4.32-dev.ota.esp32s3.bin` (nach Build) |
| Hub-Depot | `iobroker.esp-hub/firmware/pidrive.0.4.32-dev.*.esp32s3.bin` |
| Hardware | ESP32-S3-DevKitC-1 (OTG + UART) |
| Pi-Link | [PUMP.md](docs/planung/PUMP.md) · Bridge `tools/pump_bridge.py` |
| Feldbericht | [FELDTEST-ESP-MSC-BMW-2026-09-28](https://github.com/MPunktBPunkt/pidrive/blob/main/docs/betrieb/FELDTEST-ESP-MSC-BMW-2026-09-28.md) |
| Auftrag | [AUFTRAG-MSC-READS-STREAMING](docs/auftraege/AUFTRAG-MSC-READS-STREAMING.md) |

## Aktueller Fokus

1. Lab `.88`: OTA 0.4.32 + Bridge `msc_reads.jsonl` + Exporter smoke
2. Dann Auto `.89` nur wenn Lab-grün (kein USB-Callback-Netz)
3. Heimabend-Befund: ~6 s Live→HU-Cache — Synth-Trace vorhanden

## Letzte Änderung

- 2026-10-01: **0.4.32-dev** Pending-Queue + `loop()`-Drain → PUMP `msc.reads` (Burst, gap≤50 ms); `readOverflow` im Status; Bridge `/tmp/pidrive_msc_reads.jsonl`; `nbt_trace_export.py --reads`
- 2026-10-01: **0.4.31-dev** Sequential Host-Cursor
- 2026-10-01: **0.4.30-dev** B4 Head-Resync
- 2026-09-30: **0.4.29…** siehe Git-Log
