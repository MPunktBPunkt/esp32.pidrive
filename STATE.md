# STATE — esp32.pidrive

Lebender Projektstand. Kurz halten; Details in `docs/planung/`.

| Feld | Wert |
|------|------|
| Stand | 2026-09-17 |
| Phase | **Firmware 0.3.1-dev** — PUMP UART Menü-Sync + Activate |
| Repo | `MPunktBPunkt/esp32.pidrive` |
| Build | PlatformIO `env:pidrive-s3` (`pio run`) |
| Dist | `dist/pidrive.0.3.1-dev.usb.esp32s3.bin` / `.ota.esp32s3.bin` |
| Hardware | ESP32-S3-DevKitC-1 (OTG + UART) |
| Pi-Link V1 | **PUMP line-JSON** — [PUMP.md](docs/planung/PUMP.md) · Bridge `tools/pump_bridge.py` |
| Auto-Link | USB-MSC FAT12 · Slot-Namen aus Pi-Menü (max. 4) |
| Car-Test | SoftAP WebUI — [CAR-STANDALONE.md](docs/planung/CAR-STANDALONE.md) |
| ESP-Hub | Heartbeat `fwType=pidrive`, `chipModel=esp32s3` |
| WebUI | Auto-Test (Menü-Slots) · Menü · Events · Config · OTA |
| Parallel | PiDrive `audio_output=bt` bleibt |

## Aktueller Fokus

1. ~~PUMP Hello + menu_set + play_uid→activate~~ (Lab OK)
2. ~~WebUI Menü sichtbar / live~~ (0.3.1)
3. **Live-MP3 über PUMP → MSC** (Lab 2026-09-17: USB liefert nur Demo-Ton)
4. Bridge als Pi-Dienst / `usb_pump_client`
5. Fahrzeug-Gate G-USB-0

## Letzte Änderung

- 2026-09-17: **Messung** Webradio kommt **nicht** am PC-USB an — nur Demo-FAT; Doku in [PUMP.md](docs/planung/PUMP.md)
- 2026-09-17: **0.3.1-dev** WebUI Pi-Menü auf Auto-Test; PUMP-Chip; `menuRev`
- 2026-09-17: **0.3.0-dev** PumpServer, MenuStore.setFromJson, MSC Slot-Overlay, `pump_bridge.py`
- 2026-09-17: 0.2.5 … 0.1.0 — siehe Git-Log
