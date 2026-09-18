# Lab-Batch 2026-09-18 — Tests + Features 1–5

**Firmware:** `0.4.6-dev` · ESP `.89` · Pi `.111` · Bridge systemd `pidrive_pump_bridge`

## Tests (ohne NBT)

| Test | Ergebnis |
|------|----------|
| ESP alive / SoftAP | PASS `0.4.6-dev` |
| `pidrivectl usb status` | PASS (OTG/PUMP/UART/MSC) |
| Stations-Cover E2E | PASS `src=file` (Deutschrock / Rock Antenne) |
| SoftAP `/api/lab/cover` | PASS JPEG |
| Dauerstream 90 s | PASS Underruns 0 |
| sticky ID3 midstream | PASS (Titel fest am Start; ICY ändert sticky nicht) |
| MSC LBA43 ID3 / play.guess dd | PASS |
| Bridge reconnect | PASS (via systemd) |
| Menü→Bibliothek paging | FAIL (war tief in Webradio; SoftAP Home behebt) |
| Library embedded | SKIP (kein Bibliothek-Node im aktuellen Pfad) |
| Cover-Switch default | Race in Batch; manuell: RA→file, Stop→status OK |

Rohdaten früherer Batch: Pi `/tmp/lab_batch_20260918.json` (12/16).

## Features

1. **Senderlogos** — Beispiel-JPEGs unter `assets/usb-msc-covers/stations/` (5 Sender)
2. **Bridge systemd** — `systemd/pidrive_pump_bridge.service` enabled auf Pi
3. **SoftAP Remote** — Tab Fernbedienung: Home/Favoriten/Stop/Hören + großes Cover; `POST /api/lab/stop`
4. **Status-Cover** — `status/*.jpg` bei Stop (`src=status`, z. B. `bt_connected`)
5. **Favoriten-Presets** — Root-Seite: `fav0…fav2` + Menü… aus `stations.json` ★

## Verifikation live

- Root → Rock Antenne / Bayern / BOB + Menü…
- Play fav0 → `stations/web_rock_antenne.jpg`
- Stop → `status/bt_connected.jpg`
