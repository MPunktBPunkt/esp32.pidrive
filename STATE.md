# STATE — esp32.pidrive

Lebender Projektstand. Kurz halten; Details in `docs/planung/` und Feldbericht.

| Feld | Wert |
|------|------|
| Stand | 2026-10-01 |
| Phase | **Firmware 0.4.30-dev** — B4 head-resync / sticky ID3 auf Live-Fenster |
| Repo | `MPunktBPunkt/esp32.pidrive` |
| Build | PlatformIO `env:pidrive-s3` (`pio run`) |
| Dist | `dist/pidrive.0.4.30-dev.ota.esp32s3.bin` (nach Build) |
| Hub-Depot | `iobroker.esp-hub/firmware/pidrive.0.4.30-dev.*.esp32s3.bin` |
| Hardware | ESP32-S3-DevKitC-1 (OTG + UART) |
| Pi-Link | [PUMP.md](docs/planung/PUMP.md) · Bridge `tools/pump_bridge.py` |
| Feldbericht | [FELDTEST-ESP-MSC-BMW-2026-09-28](https://github.com/MPunktBPunkt/pidrive/blob/main/docs/betrieb/FELDTEST-ESP-MSC-BMW-2026-09-28.md) |
| Auftrag | [AUFTRAG-ESP-PLAY-DETECTION](https://github.com/MPunktBPunkt/pidrive/blob/main/docs/auftraege/AUFTRAG-ESP-PLAY-DETECTION.md) |

## Aktueller Fokus

1. **0.4.30:** B4 Lab `.88` (SoftAP `overlay_read` + MSC head-reread) — Feld Ohr wenn Auto wieder da
2. Feld A2 weiter Cache-limitiert (HU liest nach Burst oft nicht) — B4 hilft nur bei Re-Read/Seek
3. Link: SoftAP/UART bei schlechtem STA-RSSI

## Letzte Änderung

- 2026-10-01: **0.4.30-dev** B4: Head-Read nach Ring-Scroll remappt Audio auf `absBase_`; sticky ID3 @0; Status `absBase`/`headResyncs`; `GET /api/lab/overlay_read`
- 2026-09-30: **0.4.29-dev** Underrun→Silence-Frames; MSC-Overlay erst ab 8 KiB Ring (B5)
- 2026-09-30: **0.4.28-dev** Station-Slots: CBR-Silence + Info/Xing über volle 512 KiB (kein 0xFF-Pad)
- 2026-09-30: **0.4.27-dev** `remountGen_` load/save NVS (`pidrive`/`rm_gen`); Identity beim Boot aus NVS; Status `remountGen`/`usbSerial`
- 2026-09-30: Bridge `hello_ok_until`-Fix (kein 60‑s-Dauerblock von `menu_set`)
- 2026-09-29: **0.4.26-dev** Nav-Slots (`action`/`folder`/`pump:*`): `navMinSeqBytes=4096`; Cooldown blockiert keine Navigation
- 2026-09-28: **0.4.25–0.4.18** siehe Git-Log
