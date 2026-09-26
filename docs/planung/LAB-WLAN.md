# Lab: SoftAP / STA / Menü über WLAN + PUMP TCP

**Stand:** 2026-09-26 · FW **0.4.15-dev**  
**Ergänzt:** [CAR-STANDALONE.md](CAR-STANDALONE.md), [WEBUI.md](WEBUI.md), [PHASE-0-LAB.md](PHASE-0-LAB.md), [PUMP.md](PUMP.md)  
**PiDrive-Seite:** [`pidrive` LAB-MENU-WLAN.md](https://github.com/MPunktBPunkt/pidrive/blob/main/docs/betrieb/LAB-MENU-WLAN.md)

---

## SoftAP (Default — empfohlen am Schreibtisch)

| | |
|--|--|
| SSID | `pidrive-<MAC6>` |
| Pass | `pidrive12` |
| URL | `http://192.168.4.1/` |
| STA | default **aus** |
| PUMP TCP | **:9090** (Default an) |

Handy/Laptop mit SoftAP verbinden → WebUI: Remote, Auto-Test, **Menü (4 Slots)**, Events, Config, OTA.

Serial-Log beim Boot:

```text
[SoftAP] SSID=pidrive-…… pass=pidrive12 IP=192.168.4.1
[PUMP] TCP :9090 (same framing as UART)
```

## STA (Heim-WLAN)

Config-Tab: `STA / WiFiManager = 1` speichern. SoftAP kann parallel bleiben (`AP_STA`).  
Danach ESP unter DHCP-IP oder optional `pidrive-<MAC6>.local` (wenn mDNS an).

Pi-Settings (`usb_esp_host` / `usb_esp_port`) auf diese IP; Bridge:

```bash
python3 tools/pump_bridge.py --transport tcp --host <ESP-IP> --tcp-port 9090
```

## Live-PUMP ohne UART

Gleiches Protokoll wie Serial — Menü-Sync + Live-MP3 über TCP.  
Siehe [PUMP.md](PUMP.md) und PiDrive [LAB-MENU-WLAN.md](https://github.com/MPunktBPunkt/pidrive/blob/main/docs/betrieb/LAB-MENU-WLAN.md) §4.

## Grenzen

- SoftAP-Menü = **MSC-Slots** (max. 4 + Soft-Paging), nicht der volle PiDrive-UID-Baum.  
- Voller Baum: PiDrive `http://<pi>:8080/menu`.  
- UART bleibt parallel möglich; Responses gehen an den zuletzt aktiven Link.

## Schnellcheck

```bash
curl -sS http://192.168.4.1/api/status
curl -sS http://192.168.4.1/api/menu
```
