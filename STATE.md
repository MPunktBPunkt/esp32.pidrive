# STATE — esp32.pidrive

Lebender Projektstand. Kurz halten; Details in `docs/planung/` und Feldbericht.

| Feld | Wert |
|------|------|
| Stand | 2026-10-03 |
| Phase | **Lab 0.4.37-dev L0** (FAT16/4 MiB) auf `.88` · **Auto `.89` bleibt 0.4.36** bis B7 |
| Repo | `MPunktBPunkt/esp32.pidrive` |
| Build | `pio run -e pidrive-s3` (0.4.36) · **Lab L0:** `pio run -e pidrive-s3-l0` (0.4.37) |
| Dist | `dist/pidrive.0.4.37-dev.ota.esp32s3.bin` (L0) · `0.4.36` weiter für Auto |
| Hub-Depot | nicht für L0-Auto pushen |
| Hardware | ESP32-S3-DevKitC-1 (OTG + UART) |
| Pi-Link | [PUMP.md](docs/planung/PUMP.md) · Bridge `tools/pump_bridge.py` |
| Feldbericht | [FELDTEST-ESP-MSC-BMW-2026-09-28](https://github.com/MPunktBPunkt/pidrive/blob/main/docs/betrieb/FELDTEST-ESP-MSC-BMW-2026-09-28.md) |
| Auftrag | [AUFTRAG-L0-GEOMETRIE](docs/auftraege/AUFTRAG-L0-GEOMETRIE.md) · pidrive Host-Read-Nachweis M2 |

## Aktueller Fokus

1. **B7 am Auto** auf 0.4.36 — kein L0-OTA auf `.89`.
2. **L0 Lab** `.88`: FAT16, `sectorCount=8192`, Slots ~1 MiB — Smoke OK 2026-10-03.
3. Danach: L0_16M + statische Zeitmarker-MP3 (M3), nicht Ring.

## Letzte Änderung

- 2026-10-03: **0.4.37-dev / PIDRIVE_GEO_L0** — FAT16 4 MiB; env `pidrive-s3-l0`; OTA nur Lab `.88`
- 2026-10-02: Feld §11.10; gegen Probe-FW-Fork; Fokus B7
