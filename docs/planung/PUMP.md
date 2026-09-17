# PUMP V0.4.2 — Line-JSON + Binär-Audio + sticky ID3/APIC

**Stand:** 2026-09-17 · Firmware **0.4.2-dev**  
**Transport:** UART-Bridge / `ttyACM*`, **115200** 8N1  
**Bridge:** [`tools/pump_bridge.py`](../../tools/pump_bridge.py)  
**Cover-Spec / Assets:** [COVER-ID3.md](COVER-ID3.md) · [pidrive assets/usb-msc-covers](https://github.com/MPunktBPunkt/pidrive/tree/main/assets/usb-msc-covers)

---

## Architektur (Lab)

```
PiDrive menu.json ──► pump_bridge ──UART JSON──► PumpServer
meta.url / ffmpeg ──► pump_bridge ──UART 0x55──► StreamBuffer audio (48 KiB)
status + JPEG     ──► pump_bridge ──UART 0x56──► StreamBuffer sticky ID3 (≤12 KiB)
activate:<uid>   ◄── play_uid / WebUI
                         │
                         ▼
              UsbMscGadget (Namen + Live-LBAs, File = ID3|MP3)
                         │
                         ▼
         SoftAP /api/lab/stream|listen  ·  USB-Host MSC · Browser-Audio
```

---

## Nachrichten

| `t` / Frame | Richtung | Inhalt |
|-------------|----------|--------|
| `hello` / `hello_ack` | ↔ | `"audio":true,"bin":true` |
| `menu_set` / `menu_ack` | ↔ | bis 4 Slots |
| `event` `play_uid` | ESP→Pi | MSC Play-Guess oder Lab-Play |
| `audio_start` / `audio_stop` / `audio_ack` | ↔ | Stream; ack kann `"id3":true` |
| Binär `0x01 0x55` | Pi→ESP | MP3-Payload ≤480 B |
| Binär `0x01 0x56` | Pi→ESP | sticky ID3 append ≤480 B / Frame |

Bridge: Station mit `meta.url` → `ffmpeg -b:a 48k -ac 1 -ar 22050 -f mp3 pipe:1`.  
Vor dem Audio: ID3v2 mit TIT2/TPE1/TALB/APIC (Cover 320×320 JPEG).

---

## Cover-Größe (Kurz)

| | Empfohlen | ESP-Limit |
|--|-----------|-----------|
| Pixel | **320×320** | — |
| JPEG-Datei | ≤ **8 KiB** | ID3 gesamt ≤ **12 KiB** |
| Lab-Ist | ~4 KiB Cover, ~5,1 KiB ID3 | ok |

Details + Ordner für eigene Logos: [COVER-ID3.md](COVER-ID3.md).

---

## Lab-Verifikation

| Datum | Check | Ergebnis |
|-------|-------|----------|
| 2026-09-17 | Menü + Activate | OK |
| 2026-09-17 | Audio ohne Stream | Negativ (Demo-FAT) |
| 2026-09-17 | 0.4.0 Puffer / `/api/lab/stream` | MP3 48 kbit/s |
| 2026-09-17 | 0.4.1 `/api/lab/listen` WebUI | Browser-Audio |
| 2026-09-17 | **0.4.2 ID3/APIC am PC** | ffprobe: title/artist/album + JPEG-Cover |
| 2026-09-17 | NBT zeigt Cover | **offen** (Feldtest) |

```bash
curl -o /tmp/s.mp3 http://<ESP>/api/lab/stream
ffprobe -show_entries format_tags=title,artist,album /tmp/s.mp3
```

---

## Bridge starten (Pi)

```bash
python3 -u tools/pump_bridge.py --port /dev/ttyACM0 --bitrate 48k
# braucht: python3-serial, python3-pil, python3-mutagen, ffmpeg
```

Nach ESP-OTA Bridge neu starten. Probe-Datei: `/tmp/pump_id3_probe.mp3`.

---

## Firmware-Artefakte

| Datei | Nutzung |
|-------|---------|
| `dist/pidrive.0.4.2-dev.usb.esp32s3.bin` | Hub-/USB-Flash @ `0x0` (merged) |
| `dist/pidrive.0.4.2-dev.ota.esp32s3.bin` | SoftAP `/ota-upload` / Hub-OTA-Pull |

Ablage auch in [`iobroker.esp-hub/firmware/`](https://github.com/MPunktBPunkt/iobroker.esp-hub/tree/main/firmware).

---

## Grenzen / nächste Stufen

- 115200 ⇒ ≤48–64 kbit/s Audio
- Custom-Cover aus `pidrive/assets/usb-msc-covers/stations/`
- FAT-Host-Player / NBT-Cover-Feldtest
- Bridge als systemd / `usb_pump_client`
