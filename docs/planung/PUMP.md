# PUMP V0.3 — Line-JSON über UART

**Stand:** 2026-09-17 · Firmware **0.3.1-dev**  
**Transport:** zweiter USB (UART-Bridge / `ttyACM*`), 115200 8N1, eine JSON-Zeile pro Frame  
**Bridge (Lab):** [`tools/pump_bridge.py`](../../tools/pump_bridge.py) auf dem Pi

Ziel: PiDrive-Menü auf dem ESP darstellen (max. 4 MSC-Slots) und Play/Öffnen zurück an PiDrive steuern. Live-MP3-Stream über PUMP ist **noch nicht** Teil dieser Stufe.

---

## Architektur (Lab)

```
PiDrive  ──/tmp/pidrive_menu.json──►  pump_bridge.py  ──UART──►  PumpServer (ESP)
         ◄──/tmp/pidrive_cmd────────  (activate:<uid>) ◄─ play_uid / WebUI Lab
                                              │
                                              ▼
                                     MenuStore → UsbMscGadget (FAT-Namen)
                                              │
                                              ▼
                                     SoftAP WebUI / Auto-USB-Host
```

| Rolle | Komponente |
|-------|------------|
| ESP Server | `src/core/PumpServer.*` |
| Menü | `MenuStore::setFromJson` + `UsbMscGadget::applyMenuSlots` |
| Pi Lab-Bridge | `tools/pump_bridge.py` liest `/tmp/pidrive_menu.json`, schreibt `/tmp/pidrive_cmd` |
| WebUI | Tab Auto-Test + Menü · `POST /api/lab/play` → `pump.sendPlayUid` |

---

## Nachrichten (ESP ↔ Pi)

| `t` | Richtung | Inhalt |
|-----|----------|--------|
| `hello` | Pi → ESP | `{ "t":"hello","ver":1 }` |
| `hello_ack` | ESP → Pi | `{ "t":"hello_ack","ver":"<FW>","fw":"pidrive","slots":4 }` → `pumpUp=true` |
| `menu_set` | Pi → ESP | `{ "t":"menu_set","rev":N,"items":[{uid,name,kind},…] }` max. 4 |
| `menu_ack` | ESP → Pi | `{ "t":"menu_ack","ok":true,"n":…,"rev":… }` |
| `event` | ESP → Pi | `{ "t":"event","op":"play_uid","uid":"…" }` (MSC Play-Guess oder Lab-Play) |

`kind`: `folder` \| `station` \| `action` (Bridge mappt PiDrive-`type`, überspringt `info`).

---

## Bridge starten (Pi)

```bash
# Abhängigkeiten: python3-serial
scp tools/pump_bridge.py pidrive@<pi>:~/pump_bridge.py
python3 -u ~/pump_bridge.py --port /dev/ttyACM0 --interval 0.8
```

Nach ESP-Reboot/OTA Port kurz weg — Bridge neu starten. Log: `/tmp/pump_bridge.log` (wenn umgeleitet).

Erfolgskette: `hello_ack` → `menu_set` → `menu_ack` → WebUI zeigt Pi-Labels · Chip **PUMP ●**.

---

## Lab-Verifikation (2026-09-17)

| Schritt | Ergebnis |
|---------|----------|
| SoftAP/STA WebUI Menü | Pi-Einträge (Favoriten/Quellen/…) statt Demo |
| Lab Play Folder | `activate:<uid>` → PiDrive navigiert, neues `menu_set` |
| Lab Play Station | Webradio **Deutschrock (laut.fm)** → mpv spielt |
| MSC Slot-Namen | Overlay der ersten 4 sichtbaren Nodes |

---

## Grenzen / nächste Stufen

- Nur **4** FAT-Slots (FAT12-Demo-Image); tiefer Menübaum = Navigation über Zurück/Öffnen
- Kein MP3-Frame-Stream über UART (Ton kommt weiter vom Pi über BT/Klinke)
- Bridge noch kein systemd-Service / nicht in `pidrive` integriert (`usb_pump_client` offen)
- Nach OTA Bridge ggf. neu anbinden

Siehe auch: [PIDRIVE-INTEGRATION.md](PIDRIVE-INTEGRATION.md), [WEBUI.md](WEBUI.md).
