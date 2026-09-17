# STATE — esp32.pidrive

Lebender Projektstand. Kurz halten; Details in `docs/planung/`.

| Feld | Wert |
|------|------|
| Stand | 2026-09-17 |
| Phase | **Firmware 0.2.3-dev** — MSC LBA-Trace, Prefetch≠Play, RGB |
| Repo | `MPunktBPunkt/esp32.pidrive` |
| Build | PlatformIO `env:pidrive-s3` (`pio run`) |
| Dist | `dist/pidrive.0.2.3-dev.usb.esp32s3.bin` |
| Hardware | ESP32-S3-DevKitC-1 (OTG + UART) |
| Pi-Link V1 | PUMP — noch Stub |
| Auto-Link | USB-MSC FAT12-Demo (Ton-MP3s) live |
| Car-Test | SoftAP WebUI ohne Pi — [CAR-STANDALONE.md](docs/planung/CAR-STANDALONE.md) |
| ESP-Hub | Heartbeat `fwType=pidrive`, `chipModel=esp32s3` |
| WebUI | Auto-Test · Menü · Events · Config · OTA |
| Parallel | PiDrive `audio_output=bt` bleibt |

## Aktueller Fokus

1. Auto-Test nur ESP: SoftAP + MSC-Latenz / Play-Guess
2. Lab-Host (Debian) Enumeration bestätigen
3. Danach: PUMP-Stub + Live-MP3 vom Pi

## Letzte Änderung

- 2026-09-17: 0.2.3-dev LBA-Trace + strengere Play-Heuristik (PC-Lab)
- 2026-09-17: 0.2.2-dev RGB LED (grün idle / gelb OTG / cyan UART)
- 2026-09-17: 0.2.1-dev Port-Status AUTO/PI + klarere USB-Events
- 2026-09-17: 0.2.0-dev Car-Standalone (SoftAP, MSC, Metriken)
- 2026-09-17: 0.1.0-dev WebUI + Hub
