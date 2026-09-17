# Phase 0 / Lab — MSC an Debian/Proxmox

**Stand:** 2026-09-17  
**Ziel:** Stack (USB Device, FAT, Reads, später PUMP) **ohne** Fahrzeug debuggen.  
**Ersetzt nicht:** Stick-Spike / NBT-Evo-Timing (G-USB-0 in `pidrive`).

---

## 1. Warum Lab zuerst

- Schnelle Iteration an Enumeration, Mount, sequentiellem Lesen, Serial-Logs
- Cursor/SSH kann auf demselben Debian hosten und debuggen (`lsusb`, `dmesg`, `ttyUSB*`)
- Auto erst für HU-Read-ahead, Scan, Dateinamenslimits

---

## 2. Topologie Lab

```
Debian / Proxmox-VM (USB-Passthrough)
  │
  ├── USB-A Host  ←── ESP „UART“-Buchse   (Console + später PUMP)
  └── USB-A Host  ←── ESP OTG „USB“       (MSC Device)
```

Hinweise:

- LXC-Passthrough oft fragil → eher **VM** oder ESP direkt am Proxmox-Host.
- Beide Buchsen gleichzeitig nutzen (Bridge ≠ zweiter OTG).

---

## 3. Messreihe (sobald Firmware-Skeleton existiert)

| ID | Prüfung | Erwartung |
|----|---------|-----------|
| L1 | `lsusb` | Espressif / TinyUSB Device sichtbar |
| L2 | `dmesg` | `usb-storage` / UAS, neue `sd*`/`sg*` |
| L3 | Partition/FAT lesbar | Ordner `Stations/`, `Settings/` |
| L4 | Datei sequentiell lesen (`dd`/`mpv`) | stabile Bytes / hörbares MP3 |
| L5 | UART-Log | Read-LBA / Datei-Zuordnung |
| L6 | PUMP HELLO/STATUS | Link Pi/Lab↔ESP |
| L7 | Live-Stream 3 min | keine Underruns (nach Encode) |

---

## 4. Abgrenzung Fahrzeug

Nach L1–L5 grün: Stick-Spike + optional ESP am BMW. Lab-grün ≠ Auto-grün.

---

## 5. Debugging mit Cursor

Voraussetzung: Shell sieht dieselben Devices (`/dev/ttyUSB*`, `/dev/sd*`). Dann: Logs, Mount, Read-Tests, Heuristik-Schwellen gemeinsam eingrenzen.
