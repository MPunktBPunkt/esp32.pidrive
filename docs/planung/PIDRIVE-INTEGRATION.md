# PiDrive-Integration — `esp32.pidrive`

**Stand:** 2026-09-17  
**Produktiver Client:** Repo [`pidrive`](https://github.com/MPunktBPunkt/pidrive)  
**Dieses Repo:** Firmware + PUMP-Contract + optional Referenzclient unter `clients/`

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
| PUMP-Client | `integration/usb_pump_client.py` | PUMP-Server |
| FAT-Semantik | Export flache Liste | virtuelles FAT |
| Hub-OTA Stufe 2 Depot | optional später | `/ota-upload` |

**Kein** Code in `pidrive` von diesem Repo aus committen — nur Contract + Docs hier; Umsetzung nach Gates in `pidrive`.

---

## 3. DAB / Direct-ALSA

Bekanntes PiDrive-Thema: DAB umgeht oft PipeWire. Für `usb_gadget` muss derselbe Capture-/Resample-Pfad wie für ein zukünftiges `gateway` gelten — sonst stumm. Verweis: pidrive `AUFTRAG-DAB-AUDIOWEG.md` / bt-gateway PIDRIVE-INTEGRATION.

---

## 4. Lab- vs. Fahrzeug-Client

| Client | Zweck |
|--------|-------|
| `clients/pump_tester/` (später) | Laptop/Debian → ESP ohne volles PiDrive |
| PiDrive `usb_pump_client` | Produktiv |

Reihenfolge: Tester → Pi-Integration.
