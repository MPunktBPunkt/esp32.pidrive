# STATE — esp32.pidrive

Lebender Projektstand. Kurz halten; Details in `docs/planung/` und Feldbericht.

| Feld | Wert |
|------|------|
| Stand | 2026-10-01 |
| Phase | **Firmware 0.4.34-dev** — PUMP-Send ohne Critical (Reboot-Fix) |
| Repo | `MPunktBPunkt/esp32.pidrive` |
| Build | PlatformIO `env:pidrive-s3` (`pio run`) |
| Dist | `dist/pidrive.0.4.34-dev.ota.esp32s3.bin` (nach Build) |
| Hub-Depot | `iobroker.esp-hub/firmware/pidrive.0.4.34-dev.*.esp32s3.bin` |
| Hardware | ESP32-S3-DevKitC-1 (OTG + UART) |
| Pi-Link | [PUMP.md](docs/planung/PUMP.md) · Bridge `tools/pump_bridge.py` |
| Feldbericht | [FELDTEST-ESP-MSC-BMW-2026-09-28](https://github.com/MPunktBPunkt/pidrive/blob/main/docs/betrieb/FELDTEST-ESP-MSC-BMW-2026-09-28.md) |
| Auftrag | [AUFTRAG-MSC-READS-STREAMING](docs/auftraege/AUFTRAG-MSC-READS-STREAMING.md) |

## Aktueller Fokus

1. Lab `.88`: 0.4.34 OTA — `hello_ack` stabil, kein Uptime-Reset-Loop
2. Nächster Feldabend: 0.4.34 + echte `msc.reads` / `preWarmHostBytes`
3. B6-Cache-Hebel erst nach Timing-Beweis

## Letzte Änderung

- 2026-10-01 Abend: **0.4.34-dev** PUMP `sendRaw`: FreeRTOS-Mutex statt `portENTER_CRITICAL` + kein TCP-`flush` (Feld-Reboots / missing `hello_ack`)
- 2026-10-01 Abend: **0.4.33-dev** `preWarmHostBytes`
- 2026-10-01: **0.4.32-dev** `msc.reads` PUMP-Streaming
