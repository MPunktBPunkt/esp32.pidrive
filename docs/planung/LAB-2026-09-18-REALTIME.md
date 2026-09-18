# Lab vs. Auto-Echtzeit — Analyse & Nachtests 2026-09-18

**Firmware:** `0.4.3-dev` · ESP `192.168.178.89` · Pi `192.168.178.111`  
**Rohdaten:** Pi `/tmp/lab_realtime_20260918.json` (Kopie im Repo-Anhang unten)  
**Vorläufer:** [LAB-2026-09-18.md](LAB-2026-09-18.md) (Quellen/Paging/Cover)

---

## 1. Entspricht der bisherige Lab-Test dem Auto?

**Kurz: nur teilweise.** SoftAP + `/api/lab/play` + `/api/lab/stream` prüfen die **Pi↔ESP-Logik** (Menü, Paging, UART-Audio, ID3). Sie ersetzen **nicht** den BMW-USB-Host.

| Autopfad (NBT) | Lab bisher | Deckung |
|----------------|------------|---------|
| OTG enumerate + FAT listen | OTG am PC, SoftAP-Menü | **teilweise** — SoftAP ≠ FAT-Dateiliste |
| Datei antippen → sequentielle Sektor-Reads | `/api/lab/play` sendet `play_uid` direkt | **Lücke** — `looksLikePlay` / `play.guess` kaum geübt |
| Host dekodiert MP3 aus MSC-LBAs | SoftAP-Stream / ffprobe | **Lücke** — anderer Consumer |
| HU cached Cover/Dateinamen | sticky ID3 am Stream-Start | **Proxy** — Platzierung ok, Cache ungetestet |
| Puffer 3–8 s gegen Read-ahead | UART 48 kbit/s, Ring 48 KiB | **Proxy** — Rate ok, HU-Drain anders |
| Dateinamen in Medien-UI | MenuStore-Labels in SoftAP | **Lücke** — FAT noch 8.3 (`01ROCK.MP3` …) |

### Blocker in diesem Lab-Setup

Der Proxmox-CT sieht `USB_MEDIA` in `lsblk`/`sysfs`, hat aber **kein** `/dev/sda`. Damit kein `dd`/`mpv` auf dem Stick → **kein** echter MSC-Host-Playback und kein gezieltes Auslösen von `play.guess` über ≥8 KiB Head-Reads.

`readCount` blieb über lange Zeit bei **9** (einmaliger Scan nach Plug); `msPlugToFirstRead≈1063 ms`; `msPlugToPlayGuess=0`.

---

## 2. Nachtests (Auto-Proxys), die möglich waren

### 2.1 Menü-Sync nach Ordner-Activate

| | |
|--|--|
| Aktion | SoftAP `lab/play` auf **Quellen** |
| Ergebnis | ESP-Menü in **~400 ms** → Zurück / FM / DAB+ / Mehr… |
| Auto-Bezug | optimistic; NBT refreshed Verzeichnisse oft träger / cached |

### 2.2 Soft-Paging bis Bibliothek

| | |
|--|--|
| Ergebnis | **2 Seiten**, **~2,0 s** bis „Bibliothek“ sichtbar |
| Auto-Bezug | Im Auto wäre jede Seite ein „Track“-Play auf `Mehr…` — gleiches PUMP-Verhalten, plus HU-UI |

### 2.3 UART-Dauerstream 60 s

| | |
|--|--|
| Forward | durchgängig **~11040 B / 2 s** (~44 kbit/s Nutzlast) |
| Underruns | **0 → 0** |
| `stream.active` | true, Ring voll (49152) |
| Auto-Bezug | belegt UART-Füllrate; **nicht** BMW-Sektor-Timing |

### 2.4 Paging während Stream

| | |
|--|--|
| Ergebnis | Seite wechselte; Audio-Forward **lief weiter** |
| Auto-Bezug | `pump:page_*` injiziert nicht in PiDrive — korrekt für Mehr/Seite1 |

### 2.5 Sticky ID3 am Dateianfang

| | |
|--|--|
| `/api/lab/stream` | beginnt mit **`ID3`**, Tag **~8,2 KiB** |
| Auto-Bezug | passt zum typischen „Host liest Offset 0“; NBT-Anzeige/Cache bleibt Feldtest |

### 2.6 Audio-Start-Latenz (Messung)

| | |
|--|--|
| Status | Messung unscharf (Stream lief schon / Log-Marker ohne neues `[id3] sent`) |
| Evidenz | Stream-Head trotzdem ID3 (`X-Stream-Id3` gesetzt), 60 s-Lauf stabil |
| Auto-Bezug | Im Auto kommt **zusätzlich** MSC-Read + `play.guess` davor |

### 2.7 UART-Budget

115200 Baud ≈ 11 KB/s; 48 kbit/s Audio ≈ 6 KB/s; ID3 8 KiB ≈ **~0,7 s** einmalig — Budget ok.

---

## 3. Was nur im Auto (oder mit Host-`/dev/sda`) geht

1. **G-USB-0 / Stick-Spike:** NBT listet und spielt Gadget-MP3  
2. **`play.guess` echt:** sequentielle File-Reads vom HU  
3. **Hörbarer Ton am Autoradio** (nicht SoftAP)  
4. **Dateinamen-UX:** 8.3 vs. gewünschte Labels / LFN  
5. **Cover am Display** inkl. Cache-Verhalten  
6. **Umschaltzeit** Sender↔Sender (Dension 1–5 s)  
7. **Prefetch-False-Activate** (HU liest voraus ohne Play)

Empfehlung für den nächsten Lab-Schritt: OTG an eine **VM/Host mit Device-Node** (`/dev/sda` mountbar) — dann `dd`/`ffmpeg -i /mnt/…/01ROCK.MP3` als BMW-Proxy.

**Erledigt 2026-09-18 (vormittags):** beide ESP-Buchsen am Pi (`.111`) → siehe §6.

---

## 4. Bewertung der gestrigen/heutigen Quellentests

| Test | Lab-gültig? | Auto-gültig? |
|------|-------------|--------------|
| Soft-Paging erreichbar | ja | tendenziell ja (als virtuelle Tracks) |
| Webradio UART+ID3 | ja | nur wenn HU Stream-Datei liest |
| Library embedded APIC | ja (SoftAP) | unklar bis NBT Cover zeigt |
| Spotify Toggle | ja (Activate) | Menü ja, Ton über USB nein |
| DAB/FM | n/a (keine Antenne) | Feld |
| Stop / Audio-Route-Menü | ja | ja, sofern Dateien sichtbar |

---

## 5. JSON-Kern (Nachtest)

```json
{
  "menu_sync_ms": 400,
  "paging_bibliothek_ms": 2048,
  "uart_60s_underruns": [0, 0],
  "uart_fwd_B_per_2s": 11040,
  "sticky_id3_B": 8204,
  "msc_readCount": 9,
  "msPlugToFirstRead": 1063,
  "play_guess_exercised": false
}
```

Vollständige Datei auf dem Pi: `/tmp/lab_realtime_20260918.json`.

---

## 6. MSC-Host am Pi (beide ESP-Seiten)

**Setup:** OTG → Pi `/dev/sda` (`PIDRIVE` / `USB_MEDIA`), UART → `/dev/ttyACM0`, Bridge + PUMP aktiv.  
**ACL:** User `pidrive` in Gruppe `disk` + udev `99-pidrive-msc.rules` (`/home/pidrive/bin/enable-msc-access.sh`).  
**Rohdaten:** [lab_msc_host_20260918.json](lab_msc_host_20260918.json)

| Test | Ergebnis |
|------|----------|
| Links otg+uart+pump | ok |
| FAT mount / Liste | `STATIONS/01ROCK.MP3` … (8.3), Dateien ~6,5 KiB sticky |
| `play.guess` | ok — `dd iflag=direct` ≥16 KiB ab Slot-LBA → Event + `msPlugToPlayGuess` gesetzt |
| Live-Stream (Deutschrock) | `stream.active`, `id3Len=8204`, Underruns **0** beim Capture |
| MSC-Head + APIC | ID3 **8204 B**, Frame `APIC:Cover` **7052 B** JPEG, TIT2 ok |
| ffmpeg 1 s nach ID3-Cut | ok |

### Was das schließt / was offen bleibt

| Lücke aus §1 | Status nach Pi-Host |
|--------------|---------------------|
| Mount + Dateiliste | **geschlossen** (Lab) |
| `play.guess` über Sektor-Reads | **geschlossen** (Lab-Proxy; ≠ NBT-Heuristik) |
| Host dekodiert MP3 aus MSC | **geschlossen** (ffmpeg am Pi) |
| Sticky ID3 + embedded APIC am Stick | **geschlossen** (mutagen am Capture) |
| FAT 8.3 vs. SoftAP-Labels | bestätigt: Host sieht 8.3 |
| NBT Cover-Cache / Hörbarkeit | **weiterhin nur Auto** |

```json
{
  "play_guess": true,
  "stream_id3Len": 8204,
  "apic_B": 7052,
  "ffmpeg_1s": true,
  "fat_example": "STATIONS/01ROCK.MP3"
}
```
