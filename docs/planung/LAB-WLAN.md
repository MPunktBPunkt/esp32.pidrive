# Lab: SoftAP / STA / Menü über WLAN

**Stand:** 2026-09-25  
**Ergänzt:** [CAR-STANDALONE.md](CAR-STANDALONE.md), [WEBUI.md](WEBUI.md), [PHASE-0-LAB.md](PHASE-0-LAB.md)  
**PiDrive-Seite:** [`pidrive` LAB-MENU-WLAN.md](https://github.com/MPunktBPunkt/pidrive/blob/main/docs/betrieb/LAB-MENU-WLAN.md) (Browser-Menübaum am Pi)

---

## SoftAP (Default — empfohlen am Schreibtisch)

| | |
|--|--|
| SSID | `pidrive-<MAC6>` |
| Pass | `pidrive12` |
| URL | `http://192.168.4.1/` |
| STA | default **aus** |

Handy/Laptop mit SoftAP verbinden → WebUI: Remote, Auto-Test, **Menü (4 Slots)**, Events, Config, OTA.

Serial-Log beim Boot:

```text
[SoftAP] SSID=pidrive-…… pass=pidrive12 IP=192.168.4.1
```

## STA (Heim-WLAN)

Config-Tab: `STA / WiFiManager = 1` speichern. SoftAP kann parallel bleiben (`AP_STA`).  
Danach ESP unter DHCP-IP oder optional `pidrive-<MAC6>.local` (wenn mDNS an).

Pi-Settings (`usb_esp_host` / `usb_esp_port`) auf diese IP zeigen lassen für `/api/status`-Poll.

## Grenzen

- SoftAP-Menü = **MSC-Slots** (max. 4 + Soft-Paging), nicht der volle PiDrive-UID-Baum.  
- Voller Baum: PiDrive `http://<pi>:8080/menu`.  
- Live-Sync Pi→ESP-Slots: **PUMP über UART** (`pump_bridge`), nicht SoftAP allein.

## Schnellcheck

```bash
curl -sS http://192.168.4.1/api/status
curl -sS http://192.168.4.1/api/menu
```
