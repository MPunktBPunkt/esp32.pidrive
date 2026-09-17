# STATE — esp32.pidrive

Lebender Projektstand. Kurz halten; Details in `docs/planung/`.

| Feld | Wert |
|------|------|
| Stand | 2026-09-17 |
| Phase | **Firmware 0.4.1-dev** — WebUI Live-Audio hören |
| Repo | `MPunktBPunkt/esp32.pidrive` |
| Build | PlatformIO `env:pidrive-s3` (`pio run`) |
| Dist | `dist/pidrive.0.4.0-dev.usb.esp32s3.bin` / `.ota.esp32s3.bin` |
| Hardware | ESP32-S3-DevKitC-1 (OTG + UART) |
| Pi-Link V1 | **PUMP** line-JSON + Binärframes — [PUMP.md](docs/planung/PUMP.md) |
| Auto-Link | USB-MSC · Slot-Namen + Live-Stream im aktiven Slot |
| Car-Test | SoftAP WebUI — [CAR-STANDALONE.md](docs/planung/CAR-STANDALONE.md) |
| ESP-Hub | Heartbeat `fwType=pidrive`, `chipModel=esp32s3` |
| WebUI | Auto-Test · Menü · Events · Config · OTA · `GET /api/lab/stream` |
| Parallel | PiDrive `audio_output=bt` bleibt |

## Aktueller Fokus

1. ~~PUMP Menü + Activate~~ · ~~WebUI Menü~~
2. ~~Live-MP3 Pi→ESP (48 kbit/s, Ringpuffer)~~ Lab OK via `/api/lab/stream`
3. Host-Player am USB-Stick (FAT-Größe/Chain) im Auto/PC verifizieren
4. Bridge als Pi-Dienst / `usb_pump_client`
5. Fahrzeug-Gate G-USB-0

## Letzte Änderung

- 2026-09-17: **0.4.1-dev** WebUI „Stream hören“ → `/api/lab/listen` (Browser-Audio)
- 2026-09-17: **0.4.0-dev** StreamBuffer, Binärframes, Bridge/ffmpeg 48k
- 2026-09-17: Messung 0.3.1 — ohne Stream nur Demo-FAT
- 2026-09-17: **0.3.1-dev** WebUI Pi-Menü · PUMP-Chip
- 2026-09-17: **0.3.0-dev** PumpServer, MenuStore, MSC-Slots, Bridge
