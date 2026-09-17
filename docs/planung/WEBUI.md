# WebUI — `esp32.pidrive`

**Stand:** 2026-09-17 · Entwurf  
**Bezug:** [KONZEPT.md](KONZEPT.md), [HUB-INTEGRATION.md](HUB-INTEGRATION.md), Familie `esp-hub-base` / `esp32.ergo`  
**Ziel:** schlanke Diagnose- und Setup-UI am ESP; **kein** zweites Infotainment.

---

## 1. Familien-Standard (unverändert)

Wie die übrigen Nodes:

| Tab | Pflicht | Inhalt |
|-----|---------|--------|
| **Config** | ja | WLAN/Hub, Gerätename, PUMP-Transport, Puffer-/Timing-Parameter |
| **OTA** | ja | `POST /ota-upload`, Version, freier Slot, Hinweis bei `OTA_PENDING` (Defer während STREAMING) |

Zusätzlich braucht dieses Gerät **produkt-spezifische** Tabs — Vorschlag unten.

---

## 2. Empfohlene Tab-Struktur

| Reihenfolge | Tab | Rolle |
|-------------|-----|--------|
| 1 (Default) | **Auto-Test** | SoftAP-Zugang, MSC-Timing (Plug→Read→Play), Metriken |
| 2 | **Menü** | Spiegel des virtuellen FAT / Senderliste |
| 3 | **Events** | Ringpuffer inkl. `usb.*` / `msc.*` / `play.guess` |
| 4 | **Config** | SoftAP/STA, Hub, Lab |
| 5 | **OTA** | Upload auch über SoftAP |

```
┌──────────────────────────────────────────────────────────┐
│  esp32.pidrive · v0.2 · [USB ●] [MSC ●] [LAT 340ms]     │
├──────────────────────────────────────────────────────────┤
│  Auto-Test  │  Menü  │  Events  │  Config  │  OTA        │
└──────────────────────────────────────────────────────────┘
```

Car-ohne-Pi: siehe [CAR-STANDALONE.md](CAR-STANDALONE.md).

---

## 3. Tab **Menü** (Haupt-Tab) — Filebrowser

**Idee:** Das, was der BMW (oder Lab-Host) als Stick sieht, als Baum/Liste anzeigen.

### 3.1 Inhalt

- Ordner/Dateien aus dem aktuellen virtuellen FAT (`Stations/`, `Settings/`, …)
- Pro Eintrag: Anzeigename, optional `uid`, `kind` (station / action)
- **Aktuell gespielt** hervorheben (aus Read-Heuristik / `active_name`)
- Meta-Zeile: Anzahl Einträge, letzte `MENU_SET`-Zeit, Rebuild-Sperre ja/nein

### 3.2 Was der Tab bewusst **nicht** ist

- Keine zweite Menü-Logik (kein Editieren der PiDrive-Semantik auf dem ESP)
- Kein Ersatz für PiDrive-WebUI
- V1: **read-only** — kein Umbenennen/Löschen im Browser

### 3.3 Lab-Hilfen (sinnvoll, schaltbar)

Unter Config oder als kleine Toolbar nur wenn `labMode=1`:

- **Simulate play** auf einem Eintrag → gleiches `EVENT play_uid` wie bei MSC-Read (ohne Auto)
- **Force rebuild** Anzeige aktualisieren

Damit ist der Filebrowser der schnellste Check: „Sieht der Stick aus wie erwartet, bevor ich ins Auto gehe?“

### 3.4 API (Skizze)

| Endpoint | Zweck |
|----------|--------|
| `GET /api/menu` | JSON-Baum/Liste `{path, name, uid?, kind?, playing?}` |
| `GET /api/status` | kompakt für Statusleiste (siehe §5) |
| `POST /api/lab/play` | nur labMode — Simulate play |

SSE oder kurzes Polling (1–2 s) für `playing` + Statusleiste — kein schweres Framework.

---

## 4. Tab **Events** — Debug ohne Seriell-Kabel

Genau der Nutzen für Lab an Debian/Proxmox und später im Auto (Handy am SoftAP/STA).

### 4.1 Event-Arten (Whitelist, nicht alles loggen)

| Code | Bedeutung |
|------|-----------|
| `usb.enumerated` / `usb.gone` | BMW/Lab-Host hat MSC gesehen / getrennt |
| `pump.up` / `pump.down` | PiDrive/Tester-Link |
| `pump.hello` | Version/Fähigkeiten |
| `menu.set` | neue Liste übernommen (n Items) |
| `stream.start` / `stream.stop` | uid/name |
| `stream.underrun` / `stream.low` | Puffer-Warnung |
| `play.guess` | Heuristik: Datei X wird gelesen |
| `event.sent` | `play_uid` / `next` / `action:…` an Pi |
| `ota.pending` / `ota.done` / `ota.fail` | Hub/Upload |
| `error.*` | knappe Fehlercodes |

### 4.2 UI

- Chronologische Liste (neu oben), ~100–200 Einträge Ringpuffer im RAM
- Filter-Chips: USB · PUMP · Stream · Play · OTA · Error
- Button **Clear** / **Download** (`GET /api/events?format=json` oder `.txt`)
- Keine Full-`printf`-Flood — Seriell bleibt für Entwickler; WebUI = **kuratierte** Events

### 4.3 API

| Endpoint | Zweck |
|----------|--------|
| `GET /api/events?since=<seq>` | neue Events |
| `DELETE /api/events` | Clear |
| SSE `/api/events/stream` | optional Live |

---

## 5. Statusleiste (über allen Tabs)

Kompakte Ampeln — beantwortet „läuft die Kette?“ ohne Events zu öffnen:

| Indikator | Grün | Gelb | Rot/Grau |
|-----------|------|------|----------|
| **USB** | enumerated | — | getrennt |
| **PUMP** | Link + Heartbeat | verbunden, idle | down |
| **BUF** | buffer ≥ Ziel | unter Soft-Limit | Underrun kürzlich |
| **PLAY** | active Dateiname (Text) | — | idle |

Zusätzlich: IP, Version, Uptime (klein, sekundär).

`ios` für esp-hub kann dieselben Keys spiegeln (`usbEnumerated`, `pumpState`, `bufferMs`, `activeName`, `otaState`) — Hub-Liste und WebUI konsistent.

---

## 6. Tab **Config**

Familienfelder + Gerät:

| Gruppe | Felder |
|--------|--------|
| Hub | `hub_host`, `hub_port`, Gerätename |
| PUMP | Transport UART/CDC vs. WLAN; Baud oder WiFi-Creds/PSK |
| Audio/USB | Ziel-Vorpuffer ms; Timing-Profil (K61-Äquivalent später); labMode |
| Wartung | Neustart; Factory-Defaults (ohne Hub-Identität zu löschen, analog ergo `esphub`-NS) |

Speichern → NVS; kritische USB-Parameter erst nach Hinweis (Enumeration kann kurz weg sein).

---

## 7. Tab **OTA**

Standard:

- Aktuelle Version / freier Sketch
- Drag&Drop / Datei-Upload → `POST /ota-upload`
- Wenn STREAMING: Upload akzeptieren → **Defer** → Anzeige „OTA pending, startet nach Stream-Ende“
- Link-Hinweis: auch Hub-Push (Carport)

---

## 8. Flash- / UX-Budget

- Eine HTML-Seite + wenig JS (wie Familie), **kein** React
- Menü-JSON nur aktuelle flache Liste (MVP), nicht 287-Knoten-Vollbaum
- Event-Ring fest begrenzt
- WebUI nur im Setup/Diagnose-WLAN nötig — im Dauerbetrieb Auto oft ungenutzt; trotzdem leicht halten wegen Coexistence mit MSC+PUMP

---

## 9. Abgrenzung PiDrive-WebUI

| | ESP-WebUI | PiDrive-WebUI |
|--|-----------|---------------|
| Menü bearbeiten | nein | ja |
| Quellen starten | nur Lab-Simulate | ja |
| Stick-Ansicht | **ja** | nein |
| USB/PUMP-Ampeln | **ja** | später Status-Spiegel |
| OTA dieses Chips | **ja** | Depot-Push optional |

---

## 10. Umsetzungsschritte

1. Statusleiste + `/api/status` + Config/OTA (Familie)
2. Events-Tab + Ringpuffer (sofort wertvoll im Lab)
3. Menü-Tab sobald virtuelles FAT existiert
4. Lab-Simulate play
5. SSE wenn Polling nervt

---

## 11. Entscheidung

| Frage | Vorschlag |
|-------|-----------|
| Filebrowser als Haupt-Tab? | **Ja** — Tab **Menü** |
| Events-Tab? | **Ja** — kuratiert, nicht Roh-syslog |
| Eigenes Status-Tab? | **Nein** in V1 — Statusleiste reicht |
| Menü editierbar? | **Nein** in V1 |
