# esp32.pidrive

ESP32-S3 USB-Medien-Gadget für PiDrive: erscheint dem Werksradio (z. B. BMW NBT Evo) als Massenspeicher mit virtuellen MP3-Dateien und liefert on-the-fly-Audio — analog zum Prinzip von Dension DAB+U.

PiDrive bleibt Gehirn (Quellen, Menü-UIDs, Encode). Der bestehende **Bluetooth-Pfad in PiDrive bleibt parallel** (`audio_output=bt` \| `usb_gadget`).

```
PiDrive (Pi 4)
  │  PUMP (UART/CDC V1, später optional WLAN)
  │  MP3-Frames + Menu + Control
  ▼
esp32.pidrive (ESP32-S3)
  │  USB Device MSC + Vorpuffer
  ▼
BMW / Werksradio USB-Host (Medienliste + MP3-Decode)
```

## Status

| | |
|--|--|
| **Phase** | **Firmware 0.1.0-dev** (WebUI + Hub; MSC/PUMP noch Stub) |
| **Stand** | [`STATE.md`](STATE.md) |
| **Build** | PlatformIO: `pio run -e pidrive-s3` |
| **Planung** | [`docs/planung/`](docs/planung/) |
| **Chip** | ESP32-S3 (USB-OTG) — **nicht** Classic-ESP32 |
| **Hub** | Heartbeat + OTA-Pull + `/ota-upload` — [HUB-INTEGRATION.md](docs/planung/HUB-INTEGRATION.md) |

Gegenstück / Herkunft der Idee: [`pidrive` Planung](https://github.com/MPunktBPunkt/pidrive/tree/main/docs/planung) (`KONZEPT-USB-MSC.md`, …).  
Schwesterprojekt (BT): [`esp32.bt-gateway`](https://github.com/MPunktBPunkt/esp32.bt-gateway).

## Was dieses Projekt ist

- TinyUSB **MSC Device** + virtuelles FAT
- On-the-fly-MP3 an den Auto-USB-Host
- **PUMP**-Protokoll zum Pi (Control / Stream / Events)
- Dünner IDF-**HubClient** → [`iobroker.esp-hub`](https://github.com/MPunktBPunkt/iobroker.esp-hub) (Flash @ `0x0`, OTA-Pull)
- Lab zuerst an Debian/Proxmox, Auto später

## Was es bewusst nicht ist

- Kein zweites Infotainment, kein Mixer, kein Quellen-Umschalter
- Kein Classic-Bluetooth / kein Ersatz für `esp32.bt-gateway`
- Kein BLE-Audio-Transport Pi↔ESP
- Kein Arduino-`esp-hub-base`-Klon (nur gleicher Hub-HTTP-Contract)
- Kein Pi-4-only-Gadget (USB-C = Power; USB-A = Host-only)

## Hardware (V1)

- ESP32-S3-Board mit **zwei USB-Buchsen** (native OTG + USB-UART-Bridge)
- OTG → BMW / Lab-Host (MSC)
- UART-USB → Pi oder Lab-PC (PUMP + Console)
- Flash: Dual-OTA (Partitionen noch zu messen)

## Build (PlatformIO)

```bash
cd esp32.pidrive
pio run -e pidrive-s3          # kompilieren
pio run -e pidrive-s3 -t upload
pio device monitor
```

WebUI nach WLAN-Setup: `http://<ESP-IP>/` — Tabs Menü / Events / Config / OTA.

## Repo-Struktur

```
esp32.pidrive/
├── platformio.ini          # env:pidrive-s3
├── src/                    # Arduino/PlatformIO Firmware
├── include/BuildFlags.h
├── docs/planung/           # Konzept, Hub, WebUI, …
├── min_spiffs.csv
└── …
```

## Nächste Schritte

1. Flash S3, Hub-Register + WebUI prüfen  
2. USB-MSC Device (TinyUSB / USBMSC)  
3. PUMP UART/CDC  

## Lizenz

GPL-3.0 — siehe [`LICENSE`](LICENSE).
