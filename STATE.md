# STATE — esp32.pidrive

Lebender Projektstand. Kurz halten; Details in `docs/planung/`.

| Feld | Wert |
|------|------|
| Stand | 2026-09-18 |
| Phase | **Firmware 0.4.3-dev** — Soft-Paging + embedded APIC Lab |
| Repo | `MPunktBPunkt/esp32.pidrive` |
| Build | PlatformIO `env:pidrive-s3` (`pio run`) |
| Dist | `dist/pidrive.0.4.3-dev.usb.esp32s3.bin` / `.ota.esp32s3.bin` |
| Hub-Depot | `iobroker.esp-hub/firmware/pidrive.0.4.3-dev.*.esp32s3.bin` |
| Hardware | ESP32-S3-DevKitC-1 (OTG + UART) |
| Pi-Link | [PUMP.md](docs/planung/PUMP.md) · Bridge `tools/pump_bridge.py` |
| Cover-Assets | [pidrive/assets/usb-msc-covers](https://github.com/MPunktBPunkt/pidrive/tree/main/assets/usb-msc-covers) · [COVER-ID3.md](docs/planung/COVER-ID3.md) |
| Lab | [LAB-2026-09-18.md](docs/planung/LAB-2026-09-18.md) · [Realtime-Gap](docs/planung/LAB-2026-09-18-REALTIME.md) |
| Auto-Link | USB-MSC · 4 Slots + Bridge-Paging · Live-Stream + sticky ID3 |
| WebUI | Auto-Test · Live-Audio · Menü · Events · Config · OTA |

## Aktueller Fokus

1. ~~Menü / Activate / Live-MP3 / ID3 / Soft-Paging / embedded Cover~~
2. Eigene Senderlogos in `assets/usb-msc-covers/stations/`
3. NBT-Feldtest: Titel + Cover sichtbar?
4. FAT mit >4 physischen Dateien / Bridge systemd

## Letzte Änderung

- 2026-09-18: Realtime-Gap-Analyse (Lab≠NBT, 60s UART, MSC-Host-Blocker)
- 2026-09-18: **0.4.3-dev** Soft-Paging (`Mehr…`/`Seite 1`); APIC aus lokaler MP3; Lab-Protokoll
- 2026-09-17: **0.4.2-dev** sticky ID3+APIC; Cover-Spec; Bins in Hub-Depot
- 2026-09-17: **0.4.1-dev** WebUI Stream hören
- 2026-09-17: **0.4.0-dev** Live-MP3 StreamBuffer
