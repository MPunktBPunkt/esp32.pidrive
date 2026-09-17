# ESP-Hub-Integration — `esp32.pidrive`

**Stand:** 2026-09-17  
**Anforderung:** Firmware-Verteilung wie die übrige `esp32.*`-Familie über [iobroker.esp-hub](https://github.com/MPunktBPunkt/iobroker.esp-hub) (Port **8093**).  
**Stack:** ESP-IDF (kein Arduino-Compile-Tab). Dünner HTTP-`HubClient` + `ota_manager`.

**Contract-Wahrheit:** `iobroker.esp-hub/main.js` (Familie verifiziert für bt-gateway @ v0.5.12) — siehe auch [`esp32.bt-gateway` HUB-INTEGRATION](https://github.com/MPunktBPunkt/esp32.bt-gateway/blob/main/docs/planung/HUB-INTEGRATION.md). Hier nur die **pidrive-spezifischen** Festlegungen; generische H-F1–H-F9 gelten gleich.

---

## 1. Pflichtpfade

| Pfad | Nutzung | Pflicht? |
|------|---------|----------|
| **USB-Flash** Hub-UI → `esptool write_flash` Default **`0x0`** | Erstflash / Recovery, **Merged**-IDF-Image | **ja** |
| **OTA-Pull** `otaUrl` in Heartbeat-Antwort | Heim-/Carport-WLAN | **ja** (Stufe 1) |
| **Register** `POST /api/register` | Inventar, Status, `ios` | **ja** |
| **Compile-Tab** (arduino-cli) | — | **nein** |
| **Lokales `POST /ota-upload`** am ESP | Feld ohne Hub-Route (PiDrive-Depot) | **ja** (Stufe 2, analog A21) |

```
ioBroker esp-hub :8093
        │  USB Merged @0x0  /  GET /firmware/*.bin  /  otaUrl
        ▼
ESP32-S3 esp32.pidrive (ESP-IDF)
  ├── HubClient (HTTP Register ~30 s)
  ├── ota_manager (NVS, Defer während STREAMING)
  ├── WebUI + POST /ota-upload   ← Stufe 2
  ├── PUMP (UART/CDC oder WLAN)
  └── TinyUSB MSC Device
```

Zwei Heartbeats getrennt:

- **Hub** ~30 s — Inventar / OTA  
- **PUMP** 1–2 s — Pi-Session (wenn Streaming)

OTA während `STREAMING` **deferren** (NVS `pending_ota_*`), nach Idle/Disconnect ausführen — gleiches Muster wie bt-gateway.

---

## 2. Geräte-Identität (Register-Felder)

| Feld | Wert (Vorschlag) | Hinweis |
|------|------------------|---------|
| `hwType` | `esp32` | Familie |
| `chipModel` | `esp32s3` | **Familien-Sperre** Hub (H-F5) — Dateiname muss passen |
| `fwType` | `pidrive` | unterscheidet von `bt-gateway` / `ergo` / … |
| `name` | z. B. `PiDrive USB` | A15-Äquivalent; Gerät ist Source of Truth |
| `version` | SemVer App | aus `esp_app_desc` |
| `ios` | JSON-String | u. a. `usbEnumerated`, `bufferMs`, `pumpState`, `otaState` |

---

## 3. Artefaktnamen (Build → Hub-Firmware-Ablage)

Vorschlag (an bt-gateway angelehnt):

| Artefakt | Verwendung |
|----------|------------|
| `pidrive.<semver>.usb.esp32s3.bin` | **Merged** Image, Hub-USB-Flash @ `0x0` |
| `pidrive.<semver>.ota.esp32s3.bin` | App-Slot für OTA-Pull / `/ota-upload` |

`chipModel` / Dateiname müssen Hub-Filter bestehen (`esp32s3` im Namen).  
`scripts/` soll später `idf.py` Merge + Umbenennung automatisieren.

---

## 4. Partitionen (Entwurf — messen vor FIX)

- Dual-OTA, **kein** Factory-Slot (Recovery = USB-Merged @ `0x0`)
- Slot-Größe an 4‑/8‑MB-Modul und TinyUSB+WiFi-Footprint anpassen — Flash-Budget-Messung vor Firmware-Vollausbau
- Datei später: `partitions/partitions_ota.csv`

---

## 5. NVS-Keys (Minimum)

`hub_host`, `hub_port` (8093), optional PSK, Gerätename, `pending_ota_*`, PUMP-Baud/WLAN-Creds (je Transport).

Setup: SoftAP oder UART-Console beim Erststart — Details im Pflichtenheft.

---

## 6. Abgrenzung Arduino-Familie

Wie bt-gateway: **kein** Voll-Port von `esp-hub-base`. Nur HTTP-Contract (Register, otaUrl-Pull, ios). Build bleibt ESP-IDF.

---

## 7. Checkliste vor erstem Hub-Gerät

- [ ] `chipModel=esp32s3` + passender Bin-Name
- [ ] Merged-Build @ `0x0` flashbar aus Hub-UI
- [ ] Register erscheint in Geräte-Liste
- [ ] OTA-Pull einmalig + NVS-Defer getestet
- [ ] `/ota-upload` aus Lab-PC oder Pi
