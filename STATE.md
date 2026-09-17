# STATE — esp32.pidrive

Lebender Projektstand. Kurz halten; Details in `docs/planung/`.

| Feld | Wert |
|------|------|
| Stand | 2026-09-17 |
| Phase | **Firmware 0.1.0-dev** — WebUI + Hub + Demo-Menü (kein MSC noch) |
| Repo | `MPunktBPunkt/esp32.pidrive` |
| Build | PlatformIO `env:pidrive-s3` (`pio run`) |
| Hardware | ESP32-S3-DevKitC-1 |
| Pi-Link V1 | PUMP — noch Stub |
| Auto-Link | USB-MSC — noch Stub (Lab-Toggle in WebUI) |
| ESP-Hub | Heartbeat `fwType=pidrive`, `chipModel=esp32s3` |
| WebUI | Menü · Events · Config · OTA |
| Parallel | PiDrive `audio_output=bt` bleibt |

## Aktueller Fokus

1. `pio run -e pidrive-s3` grün halten
2. Flash am S3, WebUI + Hub-Register prüfen
3. Als Nächstes: TinyUSB / USBMSC Device

## Letzte Änderung

- 2026-09-17: Erste Firmware 0.1.0-dev (PlatformIO Arduino)
