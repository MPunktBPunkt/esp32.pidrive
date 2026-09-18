# Cover / ID3 / APIC — `esp32.pidrive`

**Stand:** 2026-09-18 · Firmware **0.4.4-dev**  
**Cover-Assets (PiDrive-Repo):** [`pidrive/assets/usb-msc-covers/`](https://github.com/MPunktBPunkt/pidrive/tree/main/assets/usb-msc-covers)  
**Lab:** [LAB-2026-09-18.md](LAB-2026-09-18.md) — Library **embedded APIC** verifiziert

---

## Ziel

Am **Autoradio** (BMW USB-Medien) sollen neben dem Ton optional erscheinen:

- Titel / Artist / Album (ID3)
- **Albumcover** (ID3 APIC, JPEG)

Menü-Navigation bleibt über virtuelle Dateinamen; Cover ist Zusatz für Now-Playing.

**SoftAP (Lab):** Tab **Remote** (Fernbedienung) + Auto-Test Cover via `GET /api/lab/cover`.
Stop: `POST /api/lab/stop` → Status-Cover.

---

## Datenfluss

```
Priorität Cover:
  1) APIC aus lokaler MP3 (local_play / status.library_file) → resize 320px
  2) assets/usb-msc-covers/stations/<id|uid|slug>.jpg
  3) assets/usb-msc-covers/default.jpg   ← immer, wenn 1+2 fehlen
  4) generiertes Text-Cover (nur ohne default.jpg)

Status (Stop/Idle): status/<kind>.jpg → src=status
Root-Presets: stations.json favorite=true als erste MSC-Seite (fav0…)
        │
        ▼
pump_bridge.py → mutagen ID3v2 (TIT2/TPE1/TALB/APIC)
        │  audio_start: cSrc/cPath/cTry (SoftAP-Hinweis)
        │  UART 0x01 0x56 (sticky ID3, chunked ≤480 B)
        ▼
ESP StreamBuffer id3_[≤12 KiB] + audio ring
        ├─ MSC onRead → ID3 dann MP3
        ├─ GET /api/lab/stream → ID3+Audio (PC-ffprobe)
        ├─ GET /api/lab/cover → JPEG aus APIC (SoftAP-UI)
        └─ /api/status.cover → src/path/try für Dateinamen-Hinweis
```

---

## Größenlimits (ESP)

| Konstante | Wert | Bedeutung |
|-----------|------|-----------|
| `StreamBuffer::kId3Max` | **12 288 B (12 KiB)** | sticky ID3 inkl. APIC |
| `StreamBuffer::kCapacity` | **48 KiB** | Live-MP3-Ring (~8 s @ 48 kbit/s) |
| UART-Frame | ≤ **480 B** Payload | `0x01 0x55` Audio / `0x01 0x56` ID3 |
| JPEG resize (Bridge) | ≤ **8000 B**, max. Seite **320** | `resize_jpeg` in `pump_bridge.py` |

**SoftAP-UI:** Cover über `GET /api/lab/cover` (JPEG aus sticky APIC) und Anzeige im Tab Auto-Test (ab **0.4.4-dev**).

### Empfohlene Cover-Datei

| | |
|--|--|
| Pixel | **320×320** |
| Format | JPEG |
| Qualität | ~70 |
| Dateigröße | **≤ 6–8 KiB** |
| ID3 gesamt | **≤ 12 KiB** |

Lab 0.4.2: Cover 320×320 ≈ 3989 B, ID3 gesamt 5126 B — verifiziert am PC.

---

## Protokoll

1. `{"t":"audio_start","uid":"…"}` → ESP `stream.start` + `clearId3`
2. Bridge sendet ID3 in einem oder mehreren Frames `0x01 0x56 | len_lo | len_hi | payload`
3. Bridge streamt MP3 `0x01 0x55 | …`
4. `audio_stop` beendet

`hello_ack` enthält `"audio":true,"bin":true`; `audio_ack` kann `"id3":true` setzen.

---

## Lab-Verifikation (PC)

```bash
# nach Station-Play, Bridge läuft:
curl -D- -o /tmp/s.mp3 http://192.168.178.89/api/lab/stream
# Header: X-Stream-Id3: 5126

ffprobe -hide_banner -show_entries format_tags=title,artist,album /tmp/s.mp3
# title=…  artist=…  album=…
```

Ergebnis Lab 2026-09-17 (Rock Antenne):

| Tag | Wert |
|-----|------|
| title | All you zombies |
| artist | The Hooters |
| album | Rock Antenne |
| APIC | JPEG 320×320 |

---

## Eigene Bilder ablegen

Siehe Spec + Dateinamen in  
https://github.com/MPunktBPunkt/pidrive/tree/main/assets/usb-msc-covers

Kurz:

- `stations/<node_id>.jpg` — Senderlogo  
- `status/wifi.jpg`, `status/bt_connected.jpg`, `status/dab_scan.jpg` — Zustände  

Bridge-Lookup: APIC aus MP3 → `stations/*.jpg` → Text-Fallback.  
`status/*.jpg` ist spezifiziert, aber in der Bridge noch nicht verdrahtet.

---

## Autoradio-Hinweise

- iDrive cached Cover oft — Wechsel ohne neue Datei/UID unsicher
- Zu große APIC (&gt;1000 px) am BMW oft unsichtbar
- Feldtest NBT noch offen; PC-Pfad ist grün

Firmware-Artefakte: `dist/pidrive.0.4.4-dev.{usb,ota}.esp32s3.bin`  
Hub-Depot: `iobroker.esp-hub/firmware/pidrive.0.4.4-dev.*.esp32s3.bin`
