# STATE — esp32.pidrive

Lebender Projektstand. Kurz halten; Details in `docs/planung/`.

| Feld | Wert |
|------|------|
| Stand | 2026-09-17 |
| Phase | Planung V0.2 — Konzept + WebUI-Skizze |
| Repo | `MPunktBPunkt/esp32.pidrive` |
| Firmware | **nicht gestartet** |
| Hardware | ESP32-S3 (OTG + UART-Bridge); Dual-USB-Board bevorzugt |
| Pi-Link V1 | PUMP über USB-UART/CDC |
| Auto-Link | USB-MSC Device |
| ESP-Hub | **Pflicht** — `fwType=pidrive`, `chipModel=esp32s3` |
| WebUI | Menü · Events · Config · OTA — [docs/planung/WEBUI.md](docs/planung/WEBUI.md) |
| Parallel | PiDrive `audio_output=bt` bleibt; USB additiv |

## Aktueller Fokus

1. Lab-Enumeration MSC an Debian/Proxmox
2. HubClient + Dual-OTA-Partitionen
3. WebUI: zuerst Statusleiste + Events, dann Menü-Browser
4. Gates G-USB-0… (Fahrzeug-Spike in `pidrive`)

## Blocker / Risiken

1. NBT-Evo-Verhalten nur im Auto messbar (Lab ersetzt das nicht)
2. Firmware/HubClient noch nicht vorhanden
3. Owner-Fragen Q-USB-1/4 in `OFFENE-PUNKTE.md`

## Letzte Änderung

- 2026-09-17: WebUI-Tab-Konzept (Menü-Filebrowser, Events, Config, OTA)
- 2026-09-17: Repo angelegt; Konzept/Idee/Pfad; Hub-Pflicht
