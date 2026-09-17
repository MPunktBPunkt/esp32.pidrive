# Car-Standalone: ESP ohne Pi im Auto testen

Ziel: **nur den ESP32-S3** ans Werksradio stecken und mit dem Handy diagnostizieren — ohne Raspberry Pi, ohne PUMP, ohne Home-WLAN.

## Was die FW liefert (0.2.0-dev)

| Funktion | Nutzen im Auto |
|----------|----------------|
| **USB MSC** (TinyUSB, `ARDUINO_USB_MODE=0`) | Radio sieht Stick `PIDRIVE / USB_MEDIA` mit FAT12-Demo |
| **Demo-MP3s** (Ton-Dateien) | Radio kann Dateien listen & abspielen → hörbarer Proof |
| **SoftAP** (`pidrive-<MAC6>`, Pass `pidrive12`) | Handy → WebUI unter `http://192.168.4.1/` |
| **Events** | `usb.otg.up/down`, `usb.uart.up/down`, `msc.*`, `play.guess` mit Timestamps |
| **Metriken** | Plug→First-Read, Plug→Play-Guess, Read-Count, Last-LBA |
| **Port-UI** | Auto-Test: zwei Kacheln AUTO (OTG) + PI (UART) |
| **OTA über SoftAP** | Neue Bin ohne Hub flashen |

STA/WiFiManager ist **default aus** (`enableSta=false`), damit Boot nicht auf Home-WLAN wartet.

## Ablauf Auto-Test

1. Firmware flashen (UART-USB am PC).
2. OTG-USB → BMW USB-Host (Medienanschluss).
3. Handy mit SoftAP verbinden → Browser `http://192.168.4.1/`.
4. Tab **Auto-Test**: USB/MSC-Chips, Latenz, SoftAP-Daten.
5. Radio: USB-Medien öffnen → Menü `STATIONS/` / `SETTINGS/` prüfen.
6. Track wählen → hörbarer Ton + Event `play.guess` + LAT-Chip.
7. Events-Tab: Reihenfolge und ms-Deltas notieren.

## Events (wichtig für Reaktionszeit)

| Code | Bedeutung |
|------|-----------|
| `msc.ready` | Gadget gestartet |
| `usb.otg.up` / `usb.otg.down` | Auto-Host hat OTG gemountet / getrennt (echter USB-Event) |
| `usb.otg.suspend` / `usb.otg.resume` | Host Suspend/Resume |
| `usb.uart.up` / `usb.uart.down` | UART-Seite: Seriellaktivität / Idle (kein physischer Stecker-Sensor) |
| `msc.first_read` | Detail = ms seit Plug |
| `play.guess` | ≥2 KiB sequentiell in eine MP3-Cluster-Range |
| `msc.stream` | LBA + Bytes der aktuellen Sequenz |
| `msc.host.start` / `msc.host.stop` | Host START/STOP (Eject) |

**Wichtig:** Nur die **OTG**-Buchse (Auto) liefert echte Plug/Unplug-Events. Die **UART**-Buchse (Pi/PC) geht über einen Bridge-Chip — dort zeigt die FW Seriellaktivität als Link (Idle nach 4 s).

Heuristik Play ≠ 100 % sicher (Radio kann prefetchen), aber gut genug für erste Latenz- und Enumerations-Tests.

## Config im Auto

- SoftAP anlassen.
- STA nur einschalten, wenn Hub/Home-WLAN gebraucht wird (dann SoftAP bleibt parallel `AP_STA`).
- Lab-Mode: WebUI kann Play simulieren (ohne Radio).

## Hardware-Hinweis

Board mit **zwei USB-Buchsen**: OTG → Auto, UART → optional PC/Console. Mit nur OTG (bus-powered) reicht SoftAP für Logs — UART nicht nötig für den ersten Test.

## Grenzen (bewusst)

- Kein Live-Stream vom Pi (PUMP folgt).
- Demo-FAT fest im Flash (256 KiB FAT12).
- Write vom Host wird verworfen (read-only Image).
