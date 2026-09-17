# PUMP V0.4 — Line-JSON + Binär-Audio über UART

**Stand:** 2026-09-17 · Firmware **0.4.0-dev**  
**Transport:** UART-Bridge / `ttyACM*`, **115200** 8N1  
**Bridge:** [`tools/pump_bridge.py`](../../tools/pump_bridge.py) (Menü + ffmpeg→Frames)

---

## Architektur (Lab)

```
PiDrive menu.json ──► pump_bridge ──UART JSON──► PumpServer
meta.url / ffmpeg ──► pump_bridge ──UART BIN──► StreamBuffer (48 KiB)
activate:<uid>   ◄── play_uid / WebUI
                         │
                         ▼
              UsbMscGadget (Namen + Live-LBAs)
                         │
                         ▼
              SoftAP /api/lab/stream  ·  USB-Host MSC
```

---

## Nachrichten

| `t` / Frame | Richtung | Inhalt |
|-------------|----------|--------|
| `hello` / `hello_ack` | ↔ | Handshake; ack enthält `"audio":true,"bin":true` |
| `menu_set` / `menu_ack` | ↔ | bis 4 Slots |
| `event` `play_uid` | ESP→Pi | MSC Play-Guess oder Lab-Play |
| `audio_start` / `audio_stop` / `audio_ack` | ↔ | Stream an/aus für `uid` |
| Binär | Pi→ESP | `0x01 0x55 \| len_lo \| len_hi \| payload` (≤512 B MP3) |

Bridge: Station mit `meta.url` → `ffmpeg -b:a 48k -ac 1 -ar 22050 -f mp3 pipe:1`.

---

## Lab-Verifikation

| Datum | Check | Ergebnis |
|-------|-------|----------|
| 2026-09-17 | Menü + Activate | OK (0.3.x) |
| 2026-09-17 | USB-Audio ohne Stream | **Negativ** — nur Demo-MP3 ~1,6 s |
| 2026-09-17 | 0.4.0 Puffer füllt | `stream.size=49152`, Bridge ~11 KiB/2 s |
| 2026-09-17 | `GET /api/lab/stream` | **OK** — ffprobe: MP3 48 kbit/s mono 22050 Hz (Rock Antenne) |
| 2026-09-17 | PC mount `/dev/sda` Play | in LXC kein Blockgerät — am Host/Auto noch zu bestätigen |

```bash
# Sample aus ESP-Puffer (ohne USB-Mount):
curl -o /tmp/s.mp3 http://<ESP>/api/lab/stream && ffprobe /tmp/s.mp3
```

---

## Bridge starten (Pi)

```bash
python3 -u tools/pump_bridge.py --port /dev/ttyACM0 --bitrate 48k
# --no-audio  → nur Menü/Activate
```

Nach ESP-OTA Bridge neu starten.

---

## Grenzen / nächste Stufen

- 115200 ⇒ Zielbitrate **≤48–64 kbit/s**; höherer Baud optional
- FAT-DIR/Chain-Patch für große virtuelle Datei — Host-Player am Stick noch Feldtest
- Bridge kein systemd; Parallel-Ton auf Pi (mpv) bleibt

Siehe: [PIDRIVE-INTEGRATION.md](PIDRIVE-INTEGRATION.md), [WEBUI.md](WEBUI.md).
