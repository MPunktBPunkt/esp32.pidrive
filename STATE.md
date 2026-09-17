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
2. Bridge als dauerhafter Pi-Dienst / Integration in `pidrive`
3. >4 Slots / tieferes FAT; Live-MP3 über PUMP
4. Fahrzeug-Gate G-USB-0 (Stick-Spike am NBT)

## Letzte Änderung

- 2026-09-17: **0.3.1-dev** WebUI zeigt Pi-Menü auf Auto-Test; PUMP-Chip; Menü-Poll via `menuRev`
- 2026-09-17: **0.3.0-dev** PumpServer, MenuStore.setFromJson, MSC Slot-Overlay, `pump_bridge.py`
- 2026-09-17: 0.2.5-dev UART STILL vs AKTIV + PING/PONG
- 2026-09-17: 0.2.4-dev SoftAP „lädt…“ Fix
- 2026-09-17: 0.2.3-dev LBA-Trace + strengere Play-Heuristik
- 2026-09-17: 0.2.2-dev RGB LED
- 2026-09-17: 0.2.1-dev Port-Status AUTO/PI
- 2026-09-17: 0.2.0-dev Car-Standalone
- 2026-09-17: 0.1.0-dev WebUI + Hub
