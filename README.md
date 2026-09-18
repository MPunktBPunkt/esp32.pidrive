# esp32.pidrive

ESP32-S3 USB-Medien-Gadget für PiDrive: erscheint dem Werksradio (z. B. BMW NBT Evo) als Massenspeicher mit virtuellen MP3-Dateien und liefert on-the-fly-Audio — analog zum Prinzip von Dension DAB+U.

PiDrive bleibt Gehirn (Quellen, Menü-UIDs, Encode). Der bestehende **Bluetooth-Pfad in PiDrive bleibt parallel** (`audio_output=bt` \| `usb_gadget`).

```
PiDrive (Pi 4)
  │  PUMP (UART/CDC V1, später optional WLAN)
  │  Menu + Control  (+ später MP3-Frames)
  ▼
esp32.pidrive (ESP32-S3)
  │  USB Device MSC + Vorpuffer
  ▼
BMW / Werksradio USB-Host (Medienliste + MP3-Decode)
```

## Status

| | |
|--|--|
| **Phase** | **Firmware 0.4.5-dev** — SoftAP Cover + Soft-Paging + APIC |
| **Stand** | [`STATE.md`](STATE.md) |
| **Build** | PlatformIO: `pio run -e pidrive-s3` |
| **PUMP** | [`docs/planung/PUMP.md`](docs/planung/PUMP.md) · [`COVER-ID3.md`](docs/planung/COVER-ID3.md) · [Lab 09-18](docs/planung/LAB-2026-09-18.md) |
| **Cover-Assets** | [`pidrive/assets/usb-msc-covers`](https://github.com/MPunktBPunkt/pidrive/tree/main/assets/usb-msc-covers) (320×320 JPEG ≤8 KiB) |
| **Car-Test** | [`docs/planung/CAR-STANDALONE.md`](docs/planung/CAR-STANDALONE.md) |
| **Planung** | [`docs/planung/`](docs/planung/) |
| **Chip** | ESP32-S3 (USB-OTG, `ARDUINO_USB_MODE=0`) |
| **Hub** | [HUB-INTEGRATION.md](docs/planung/HUB-INTEGRATION.md) · Depot `firmware/pidrive.0.4.5-dev.*.esp32s3.bin` |
| **Dist** | `dist/pidrive.0.4.5-dev.usb.esp32s3.bin` · `.ota.esp32s3.bin` |

Gegenstück / Herkunft der Idee: [`pidrive` Planung](https://github.com/MPunktBPunkt/pidrive/tree/main/docs/planung) (`KONZEPT-USB-MSC.md`, …).  
Schwesterprojekt (BT): [`esp32.bt-gateway`](https://github.com/MPunktBPunkt/esp32.bt-gateway).

## Was dieses Projekt ist

- TinyUSB **MSC Device** + virtuelles FAT (Demo + Slot-Namen vom Pi)
- **PUMP** line-JSON über UART: Menü sync, Play-UID → PiDrive `activate:`
- SoftAP-WebUI (Auto-Test / Menü / Events / Config / OTA)
- Dünner HubClient → [`iobroker.esp-hub`](https://github.com/MPunktBPunkt/iobroker.esp-hub)
- Lab an Pi + Debian; Auto später

## Was es bewusst nicht ist

- Kein zweites Infotainment, kein Mixer, kein Quellen-Umschalter
- Kein Classic-Bluetooth / kein Ersatz für `esp32.bt-gateway`
- Kein BLE-Audio-Transport Pi↔ESP
- (noch) kein Live-MP3-Stream über PUMP — Ton kommt weiter vom Pi (BT/Klinke)

## Hardware (V1)

- ESP32-S3-Board mit **zwei USB-Buchsen** (native OTG + USB-UART-Bridge)
- OTG → BMW / Lab-Host (MSC)
- UART-USB → Pi oder Lab-PC (PUMP + Console)

## Build (PlatformIO)

```bash
cd esp32.pidrive
pio run -e pidrive-s3          # kompilieren
pio run -e pidrive-s3 -t upload
# oder OTA:
curl -F firmware=@dist/pidrive.0.3.1-dev.ota.esp32s3.bin http://<ESP-IP>/ota-upload
```

WebUI: `http://<ESP-IP>/` oder SoftAP `http://192.168.4.1/` — Tabs Auto-Test / Menü / Events / Config / OTA.

### PUMP-Bridge (Pi)

```bash
python3 -u tools/pump_bridge.py --port /dev/ttyACM0
# braucht laufendes PiDrive (/tmp/pidrive_menu.json) und python3-serial
```

## Repo-Struktur

```
esp32.pidrive/
├── platformio.ini
├── src/                    # Firmware (PumpServer, MSC, WebUI, …)
├── tools/pump_bridge.py    # Lab-Bridge Pi ↔ ESP
├── dist/                   # usb + ota Artefakte
├── docs/planung/           # Konzept, PUMP, Hub, WebUI, …
└── …
```

## Nächste Schritte

1. Bridge in PiDrive verdrahten (`usb_pump_client` / systemd)
2. Live-MP3 über PUMP + größerer MSC-Baum
3. Fahrzeug-Gate Stick-Spike / NBT

## Lizenz

GPL-3.0 — siehe [`LICENSE`](LICENSE).
