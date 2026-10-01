# STATE — esp32.pidrive

Lebender Projektstand. Kurz halten; Details in `docs/planung/` und Feldbericht.

| Feld | Wert |
|------|------|
| Stand | 2026-10-01 |
| Phase | **Firmware 0.4.35-dev** — play_uid-Replay nach PUMP-Reconnect |
| Repo | `MPunktBPunkt/esp32.pidrive` |
| Build | PlatformIO `env:pidrive-s3` (`pio run`) |
| Dist | `dist/pidrive.0.4.35-dev.ota.esp32s3.bin` (nach Build) |
| Hub-Depot | `iobroker.esp-hub/firmware/pidrive.0.4.35-dev.*.esp32s3.bin` |
| Hardware | ESP32-S3-DevKitC-1 (OTG + UART) |
| Pi-Link | [PUMP.md](docs/planung/PUMP.md) · Bridge `tools/pump_bridge.py` |
| Feldbericht | [FELDTEST-ESP-MSC-BMW-2026-09-28](https://github.com/MPunktBPunkt/pidrive/blob/main/docs/betrieb/FELDTEST-ESP-MSC-BMW-2026-09-28.md) |
| Auftrag | [AUFTRAG-MSC-READS-STREAMING](docs/auftraege/AUFTRAG-MSC-READS-STREAMING.md) |

## Aktueller Fokus

1. Lab: Trace `feld_prefetch_then_warm_gentle` / `feld_bob_2003_prewarm` — B6-Hebel gegen `pre=182272`
2. Morgen Auto: **0.4.35** OTA — ohne PUMP-Drops: kommt `streamBytes>0`?
3. B6: Warm früher / Prefetch-Silence / Cache-Key

## Letzte Änderung

- 2026-10-01 Abend: **t_ms-Export-Fix** (Feld-Traces kumulativ); Suite-Szenario `prefetch_then_warm`; Feld-Traces `feld_bob_*` / `feld_1949_session`
- 2026-10-01 Abend: **0.4.35-dev** queued `play_uid` → Replay auf `hello`
- 2026-10-01: **0.4.34-dev** Critical→Mutex Reboot-Fix · **0.4.33** `preWarmHostBytes`
