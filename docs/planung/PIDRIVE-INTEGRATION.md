# PiDrive-Integration — `esp32.pidrive`

**Stand:** 2026-09-17  
**Produktiver Client:** Repo [`pidrive`](https://github.com/MPunktBPunkt/pidrive)  
**Dieses Repo:** Firmware + PUMP-Contract + Lab-Bridge [`tools/pump_bridge.py`](../../tools/pump_bridge.py)  
**Protokoll:** [PUMP.md](PUMP.md) (0.3.1-dev Lab verifiziert)

---

## 0. Lab-Stand (2026-09-17)

Ohne fest verdrahteten `usb_pump_client` in PiDrive:

1. PiDrive normal laufen lassen (`/tmp/pidrive_menu.json` + `/tmp/pidrive_cmd`)
2. UART-Kabel ESP→Pi (`/dev/ttyACM0`)
3. `python3 tools/pump_bridge.py --port /dev/ttyACM0`
4. WebUI / MSC: Menü öffnen → Bridge schreibt `activate:<uid>`

Damit ist Menü+Navigation E2E nutzbar; Audio weiter über bestehenden Pi-Pfad (BT/Klinke). Nächster Umbau: Bridge als Dienst + `audio_output=usb_gadget`.

---

## 1. Parallelbetrieb (fest im Konzept)

PiDrive behält BlueZ/A2DP/AVRCP. USB ist additiv:

```
audio_output ∈ { auto, klinke, bt, hdmi, usb_gadget [, gateway] }
```

Zur Laufzeit **ein** Hörpfad zum BMW. Details: [KONZEPT.md](KONZEPT.md) §2–§3, [PFAD-PFLICHTENHEFT.md](PFAD-PFLICHTENHEFT.md) §1.1.

---

## 2. Was in `pidrive` gebaut wird (Umbaupakete)

Skizze U0–U8: [PFAD-PFLICHTENHEFT.md](PFAD-PFLICHTENHEFT.md) §6.

| Thema | PiDrive | esp32.pidrive |
|-------|---------|---------------|
| Quellen, UID-Menü, Trigger | ja | nein |
| MP3-Encode (Tendenz) | ja | Puffer / MSC |
| PUMP-Client | Lab: `tools/pump_bridge.py` · Soll: `integration/usb_pump_client.py` | `PumpServer` |
| FAT-Semantik | aktuelle Menü-Seite (UIDs) | virtuelles FAT, max. 4 Slots |
| Hub-OTA Stufe 2 Depot | optional später | `/ota-upload` |

Lab-Bridge liegt bewusst in **diesem** Repo (`tools/`). Produktivcode (`usb_pump_client`, systemd) später in `pidrive`.

---

## 3. DAB / Direct-ALSA

Bekanntes PiDrive-Thema: DAB umgeht oft PipeWire. Für `usb_gadget` muss derselbe Capture-/Resample-Pfad wie für ein zukünftiges `gateway` gelten — sonst stumm. Verweis: pidrive `AUFTRAG-DAB-AUDIOWEG.md` / bt-gateway PIDRIVE-INTEGRATION.

---

## 4. Lab- vs. Fahrzeug-Client

| Client | Zweck |
|--------|-------|
| `tools/pump_bridge.py` | **jetzt** — PiDrive IPC ↔ UART-PUMP |
| `clients/pump_tester/` (später) | Laptop/Debian ohne volles PiDrive |
| PiDrive `usb_pump_client` | Produktiv / systemd |

Reihenfolge: Bridge (erledigt Lab) → Pi-Integration → Fahrzeug.
