# STATE — esp32.pidrive

Lebender Projektstand. Kurz halten; Details in `docs/planung/` und Feldbericht.

| Feld | Wert |
|------|------|
| Stand | 2026-10-01 |
| Phase | **Firmware 0.4.31-dev** — B4 sequential host cursor (nach Ring-Scroll) |
| Repo | `MPunktBPunkt/esp32.pidrive` |
| Build | PlatformIO `env:pidrive-s3` (`pio run`) |
| Dist | `dist/pidrive.0.4.31-dev.ota.esp32s3.bin` (nach Build) |
| Hub-Depot | `iobroker.esp-hub/firmware/pidrive.0.4.31-dev.*.esp32s3.bin` |
| Hardware | ESP32-S3-DevKitC-1 (OTG + UART) |
| Pi-Link | [PUMP.md](docs/planung/PUMP.md) · Bridge `tools/pump_bridge.py` |
| Feldbericht | [FELDTEST-ESP-MSC-BMW-2026-09-28](https://github.com/MPunktBPunkt/pidrive/blob/main/docs/betrieb/FELDTEST-ESP-MSC-BMW-2026-09-28.md) |
| Auftrag | [AUFTRAG-ESP-PLAY-DETECTION](https://github.com/MPunktBPunkt/pidrive/blob/main/docs/auftraege/AUFTRAG-ESP-PLAY-DETECTION.md) |

## Aktueller Fokus

1. **0.4.31** Lab: SoftAP sequential cursor **PASS** (`underrun=0`, live_ratio=1 über 80 KiB past head)
2. Feld Abend: OTA Auto `.89` auf **0.4.31**; Bridge zurück auf `.89`
3. A2 bleibt Cache-limitiert, wenn HU nach Burst **nicht** weiterliest

## Letzte Änderung

- 2026-10-01: **0.4.31-dev** Sequential Host-Cursor (`hostAbsCursor_`) — Reads hinter dem Head-Fenster folgen dem Live-Ring; Status `cursorArmed`/`hostAbsCursor`; Tools `lab_b4_reread.py`, `lab_paced_consume.py`
- 2026-10-01: **0.4.30-dev** B4 Head-Resync (nur near-head Remap)
- 2026-09-30: **0.4.29-dev** Underrun→Silence; Overlay-Warmup 8 KiB
- 2026-09-30: **0.4.28…0.4.18** siehe Git-Log
