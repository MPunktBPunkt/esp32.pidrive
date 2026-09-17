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
| **Phase** | Planung / Lab-Vorbereitung — **Firmware noch nicht gestartet** |
| **Stand** | [`STATE.md`](STATE.md) |
| **Planung** | [`docs/planung/`](docs/planung/) |
| **Chip** | ESP32-S3 (USB-OTG) — **nicht** Classic-ESP32 |
| **Hub** | Pflicht — USB-Flash + OTA wie `esp32.*`-Familie ([docs/planung/HUB-INTEGRATION.md](docs/planung/HUB-INTEGRATION.md)) |

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

## Repo-Struktur

```
esp32.pidrive/
├── README.md
├── STATE.md
├── LICENSE                 # GPL-3.0
├── docs/planung/           # Konzept, Hub, Gates, …
├── main/                   # ESP-IDF App (Platzhalter)
├── components/             # hub_client, pump, msc_vfs, … (später)
├── partitions/             # Dual-OTA csv (später)
├── scripts/                # Build/Merge/Hub-Artefakte
└── clients/                # Referenz-PUMP-Tester (Python, später)
```

## Nächste Schritte

1. Lab: ESP MSC an Debian/Proxmox → siehe [`docs/planung/PHASE-0-LAB.md`](docs/planung/PHASE-0-LAB.md)
2. Stick-Spike am BMW (PiDrive-Repo) → Gate G-USB-0
3. HubClient-Skeleton + Dual-OTA-Partitionen
4. TinyUSB MSC + statische Test-MP3, dann PUMP + Live-Stream

## Lizenz

GPL-3.0 — siehe [`LICENSE`](LICENSE).
