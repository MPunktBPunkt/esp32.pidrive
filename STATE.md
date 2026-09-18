# STATE — esp32.pidrive

Lebender Projektstand. Kurz halten; Details in `docs/planung/`.

| Feld | Wert |
|------|------|
| Stand | 2026-09-18 |
| Phase | **Firmware 0.4.12-dev** — static FAT; Stream = Payload-Overlay only |
| Repo | `MPunktBPunkt/esp32.pidrive` |
| Build | PlatformIO `env:pidrive-s3` (`pio run`) |
| Dist | `dist/pidrive.0.4.12-dev.ota.esp32s3.bin` |
| Hub-Depot | `iobroker.esp-hub/firmware/pidrive.0.4.8-dev.*.esp32s3.bin` |
| Hardware | ESP32-S3-DevKitC-1 (OTG + UART) |
| Pi-Link | [PUMP.md](docs/planung/PUMP.md) · Bridge `tools/pump_bridge.py` |
| Cover-Assets | [pidrive/assets/usb-msc-covers](https://github.com/MPunktBPunkt/pidrive/tree/main/assets/usb-msc-covers) · [COVER-ID3.md](docs/planung/COVER-ID3.md) |
| Lab | [LAB-2026-09-18.md](docs/planung/LAB-2026-09-18.md) · [Realtime-Gap](docs/planung/LAB-2026-09-18-REALTIME.md) |
| Auto-Link | USB-MSC · 4 Slots + Bridge-Paging · Live-Stream + sticky ID3 |
| WebUI | **Remote** · Auto-Test · Cover · Live-Audio · Menü · Events · Config · OTA |
| Lab-Bericht (pidrive) | [USB-MSC-STREAM-LISTING-2026-09-18](https://github.com/MPunktBPunkt/pidrive/blob/main/docs/betrieb/USB-MSC-STREAM-LISTING-2026-09-18.md) |

## Aktueller Fokus

1. ~~Menü / Activate / Live-MP3 / ID3 / Soft-Paging / Cover / SoftAP-Remote~~
2. ~~Listing leer während Live-Stream (FAT-Mutation)~~ → **0.4.12 static FAT** — Lab verifizieren
3. Problem B: Phone/BMW Selection-Detector (play.guess unzuverlässig)
4. Größere virtuelle Dateien (Dauer-Stream ohne ~64 KiB-EOF)
5. Bridge systemd dauerhaft auf Pi

## Letzte Änderung

- 2026-09-18: **0.4.12-dev** static FAT/Dir/Size/Chains; Stream nur Overlay; USB write-protect; kein Remount
- 2026-09-18: **0.4.11-dev** längere FAT-Ketten; Stub-Remap; mediaPresent-Remount bei Stream
- 2026-09-18: **0.4.10-dev** STATIONS/SETTINGS LFN aus Menü-Namen
- 2026-09-18: **0.4.9-dev** DIR-Size 4MiB; play.guess nach Index-Fenster; Debounce; Bridge demo→fav
- 2026-09-18: **0.4.8-dev** Root wieder normales Menü; SoftAP kein 404; Favoriten aus stations.json ★
- 2026-09-18: **0.4.7-dev** info-Nodes (IP/SSID/BT) als MSC-Slots; Hotspot-IP-Erkennung
- 2026-09-18: **0.4.6-dev** SoftAP Remote-Tab, `/api/lab/stop`, Status-Cover, Root-Favoriten-Presets
- 2026-09-18: **0.4.5-dev** SoftAP Cover (`GET /api/lab/cover` + UI)
- 2026-09-18: Realtime-Gap-Analyse; MSC-Host am Pi
- 2026-09-18: **0.4.3-dev** Soft-Paging; APIC aus lokaler MP3
