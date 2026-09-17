# STATE — esp32.pidrive

Lebender Projektstand. Kurz halten; Details in `docs/planung/`.

| Feld | Wert |
|------|------|
| Stand | 2026-09-17 |
| Phase | Planung V0.2 — Repo-Grundstruktur, Konzept übernommen |
| Repo | `MPunktBPunkt/esp32.pidrive` |
| Firmware | **nicht gestartet** |
| Hardware | ESP32-S3 (OTG + UART-Bridge); Dual-USB-Board bevorzugt |
| Pi-Link V1 | PUMP über USB-UART/CDC |
| Auto-Link | USB-MSC Device |
| ESP-Hub | **Pflicht** — Contract wie Familie (`fwType` voraussichtlich `pidrive`, `chipModel` `esp32s3`) |
| Parallel | PiDrive `audio_output=bt` bleibt; USB additiv |

## Aktueller Fokus

1. Lab-Enumeration MSC an Debian/Proxmox
2. Hub-Artefaktnamen + Partitionstabelle festziehen
3. Gates G-USB-0… (Fahrzeug-Spike in `pidrive`)

## Blocker / Risiken

1. NBT-Evo-Verhalten nur im Auto messbar (Lab ersetzt das nicht)
2. Firmware/HubClient noch nicht vorhanden
3. Owner-Fragen Q-USB-1/4 in `OFFENE-PUNKTE.md`

## Letzte Änderung

- 2026-09-17: Repo angelegt; Konzept/Idee/Pfad aus `pidrive` übernommen; Hub-Pflicht dokumentiert
