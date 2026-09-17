# `docs/planung/` — Konzept & Weg zum Pflichtenheft

Planung für `esp32.pidrive` (USB-MSC-Medienpfad für PiDrive).

> **Lebender Stand:** [`../../STATE.md`](../../STATE.md)

| Datei | Inhalt |
|-------|--------|
| [KONZEPT.md](KONZEPT.md) | Architektur, Parallel-BT, PUMP-Skizze, FAT-MVP, Hardware |
| [IDEE-USB-MSC-MENUE.md](IDEE-USB-MSC-MENUE.md) | Idee, Dension-Analyse, Voraussetzungen P/W/O |
| [PFAD-PFLICHTENHEFT.md](PFAD-PFLICHTENHEFT.md) | Gates, Pflichtenheft-TOC, PiDrive-Umbaupakete U0–U8 |
| [HUB-INTEGRATION.md](HUB-INTEGRATION.md) | iobroker.esp-hub: USB-Flash, OTA, `fwType`/`chipModel` |
| [PIDRIVE-INTEGRATION.md](PIDRIVE-INTEGRATION.md) | Was in `pidrive` additiv gebaut wird |
| [PHASE-0-LAB.md](PHASE-0-LAB.md) | Lab an Debian/Proxmox vor dem Auto |
| [OFFENE-PUNKTE.md](OFFENE-PUNKTE.md) | Q-USB-*, Entscheidungen, Risiken |

## Kurzfassung

ESP32-S3 erscheint dem Werksradio als USB-Stick mit virtuellen MP3s; PiDrive liefert Stream und Menü über **PUMP**. Bluetooth in PiDrive bleibt ein **paralleler** `audio_output`. Verteilung der Firmware über **esp-hub** (Merged-USB @ `0x0` + OTA-Pull), analog zur `esp32.*`-Familie.

**Herkunft:** ausgearbeitet in [`pidrive/docs/planung`](https://github.com/MPunktBPunkt/pidrive/tree/main/docs/planung); dieses Repo ist die Firmware-/Hub-Heimat.

**Nicht verwechseln mit:** [`esp32.bt-gateway`](https://github.com/MPunktBPunkt/esp32.bt-gateway) (Classic-BT A2DP/AVRCP).
