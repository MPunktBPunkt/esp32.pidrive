# STATE — esp32.pidrive

Lebender Projektstand. Kurz halten; Details in `docs/planung/`.

| Feld | Wert |
|------|------|
| Stand | 2026-09-18 |
| Phase | **Firmware 0.4.6-dev** — SoftAP Remote + Status-Cover + Root-Presets |
| Repo | `MPunktBPunkt/esp32.pidrive` |
| Build | PlatformIO `env:pidrive-s3` (`pio run`) |
| Dist | `dist/pidrive.0.4.6-dev.usb.esp32s3.bin` / `.ota.esp32s3.bin` |
| Hub-Depot | `iobroker.esp-hub/firmware/pidrive.0.4.6-dev.*.esp32s3.bin` |
| Hardware | ESP32-S3-DevKitC-1 (OTG + UART) |
| Pi-Link | [PUMP.md](docs/planung/PUMP.md) · Bridge `tools/pump_bridge.py` |
| Cover-Assets | [pidrive/assets/usb-msc-covers](https://github.com/MPunktBPunkt/pidrive/tree/main/assets/usb-msc-covers) · [COVER-ID3.md](docs/planung/COVER-ID3.md) |
| Lab | [LAB-2026-09-18.md](docs/planung/LAB-2026-09-18.md) · [Realtime-Gap](docs/planung/LAB-2026-09-18-REALTIME.md) |
| Auto-Link | USB-MSC · 4 Slots + Bridge-Paging · Live-Stream + sticky ID3 |
| WebUI | **Remote** · Auto-Test · Cover · Live-Audio · Menü · Events · Config · OTA |

## Aktueller Fokus

1. ~~Menü / Activate / Live-MP3 / ID3 / Soft-Paging / Cover / SoftAP-Remote~~
2. NBT-Feldtest: Titel + Cover sichtbar?
3. Bridge systemd (`pidrive_pump_bridge.service`) dauerhaft auf Pi
4. FAT mit >4 physischen Dateien

## Letzte Änderung

- 2026-09-18: **0.4.6-dev** SoftAP Remote-Tab, `/api/lab/stop`, Status-Cover, Root-Favoriten-Presets
- 2026-09-18: **0.4.5-dev** SoftAP Cover (`GET /api/lab/cover` + UI)
- 2026-09-18: Realtime-Gap-Analyse; MSC-Host am Pi
- 2026-09-18: **0.4.3-dev** Soft-Paging; APIC aus lokaler MP3
