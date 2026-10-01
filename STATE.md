# STATE — esp32.pidrive

Lebender Projektstand. Kurz halten; Details in `docs/planung/` und Feldbericht.

| Feld | Wert |
|------|------|
| Stand | 2026-10-01 |
| Phase | **Firmware 0.4.36-dev** — B6: Overlay-Warmup=0 (sofort armieren) |
| Repo | `MPunktBPunkt/esp32.pidrive` |
| Build | PlatformIO `env:pidrive-s3` (`pio run`) |
| Dist | `dist/pidrive.0.4.36-dev.ota.esp32s3.bin` (nach Build) |
| Hub-Depot | `iobroker.esp-hub/firmware/pidrive.0.4.36-dev.*.esp32s3.bin` |
| Hardware | ESP32-S3-DevKitC-1 (OTG + UART) |
| Pi-Link | [PUMP.md](docs/planung/PUMP.md) · Bridge `tools/pump_bridge.py` |
| Feldbericht | [FELDTEST-ESP-MSC-BMW-2026-09-28](https://github.com/MPunktBPunkt/pidrive/blob/main/docs/betrieb/FELDTEST-ESP-MSC-BMW-2026-09-28.md) |
| Auftrag | [AUFTRAG-MSC-READS-STREAMING](docs/auftraege/AUFTRAG-MSC-READS-STREAMING.md) |

## Aktueller Fokus

1. Lab `.88`: 0.4.36 — Prefetch-~180 KiB nach `audio_start`: erwarten `pre≈0`, `streamBytes>0`
2. Morgen Auto: 0.4.35/36 OTA — `streamBytes>0`?
3. Weitere B6-Hebel falls Lab/Auto weiter `streamBytes=0`

## Letzte Änderung

- 2026-10-01 Abend: **0.4.36-dev** `kOverlayWarmupBytes=0` + arm on `audio_start` (B6 Hebel A)
- 2026-10-01 Abend: **t_ms-Export-Fix**; Suite `prefetch_then_warm`; Feld-Traces
- 2026-10-01 Abend: **0.4.35-dev** play_uid-Replay
