# STATE — esp32.pidrive

Lebender Projektstand. Kurz halten; Details in `docs/planung/` und Feldbericht.

| Feld | Wert |
|------|------|
| Stand | 2026-09-28 |
| Phase | **Firmware 0.4.25-dev** — Serial-Bump auch bei Unplug; Bridge Nav-Grace (Baustelle A) |
| Repo | `MPunktBPunkt/esp32.pidrive` |
| Build | PlatformIO `env:pidrive-s3` (`pio run`) |
| Dist | `dist/pidrive.0.4.25-dev.ota.esp32s3.bin` (nach Build) |
| Hub-Depot | `iobroker.esp-hub/firmware/pidrive.0.4.25-dev.*.esp32s3.bin` |
| Hardware | ESP32-S3-DevKitC-1 (OTG + UART) |
| Pi-Link | [PUMP.md](docs/planung/PUMP.md) · Bridge `tools/pump_bridge.py` |
| Feldbericht | [FELDTEST-ESP-MSC-BMW-2026-09-28](https://github.com/MPunktBPunkt/pidrive/blob/main/docs/betrieb/FELDTEST-ESP-MSC-BMW-2026-09-28.md) |
| Auftrag | [AUFTRAG-ESP-PLAY-DETECTION](https://github.com/MPunktBPunkt/pidrive/blob/main/docs/auftraege/AUFTRAG-ESP-PLAY-DETECTION.md) |

## Aktueller Fokus

1. **P0 Baustelle A (umgesetzt im Bridge-Code):** UID-Grace + Snapshot-Resync nach TCP-Reconnect — Feldtest ausstehend
2. **P0 Baustelle B:** Live-Audio vs. HU-Cache (Silence+Xing, Pacing, Pulse-Messung)
3. Feld: SoftAP-direkt/UART bevorzugen bei schlechtem STA-RSSI

## Letzte Änderung

- 2026-09-28: **0.4.25-dev** `applyUsbIdentity()` auch bei Unplug (nächster Attach ≠ `PD0001`); Bridge: UID-Grace 180 s + menu snapshot resent
- 2026-09-28: **0.4.24-dev** Remount: USB `serialNumber` `PDnnnn` + Volume-Label + 600 ms Hide; `POST /api/lab/remount`; Skip Remount während Stream
- 2026-09-28: **0.4.23-dev** Index: Stub-Head nur ~1 KiB
- 2026-09-28: **0.4.22-dev** `indexSettled_` nach `msc.quiet`
- 2026-09-28: **0.4.21-dev** virt 2 MiB / Slots 512 KiB
- 2026-09-28: **0.4.20-dev** Remount bei Menu-Namenswechsel
- 2026-09-28: **0.4.19-dev** FAT-Patch zerstört Root nicht mehr
- 2026-09-28: **0.4.18-dev** FAT-safe Namen
- 2026-09-28: **0.4.17-dev** Host-SCSI / phase / quiet / diag→Pi
