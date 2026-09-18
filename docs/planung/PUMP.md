# PUMP V0.4.3 — Line-JSON + Binär-Audio + sticky ID3/APIC + Soft-Paging

**Stand:** 2026-09-18 · Firmware **0.4.3-dev**  
**Transport:** UART-Bridge / `ttyACM*`, **115200** 8N1  
**Bridge:** [`tools/pump_bridge.py`](../../tools/pump_bridge.py)  
**Cover:** [COVER-ID3.md](COVER-ID3.md) · Lab [LAB-2026-09-18.md](LAB-2026-09-18.md)

---

## Architektur (Lab)

```
PiDrive menu.json ──► pump_bridge (page 3+Mehr) ──UART JSON──► PumpServer
meta.url | local_play file ──► ffmpeg ──UART 0x55──► StreamBuffer audio
APIC|stations.jpg|generated ──► ID3 ──UART 0x56──► sticky ID3 ≤12 KiB
activate / pump:page_*   ◄── play_uid
```

---

## Soft-Paging (V0.4.3)

MSC bleibt bei **4 Slots**. Die Bridge splittet Ordner mit >4 Einträgen:

| Slot | Inhalt |
|------|--------|
| 1–3 | aktuelle Menüknoten |
| 4 | `Mehr… (+N)` → uid `pump:page_next` **oder** `Seite 1` → `pump:page_home` |

`pump:page_*` wird **nicht** an PiDrive injiziert — nur Seitenwechsel + neues `menu_set`.

`hello_ack` enthält `"page":true`.

---

## Cover-Priorität

1. **APIC** aus lokaler MP3 (`local_play` / `library_file`) — resize 320×320 ≤8 KiB  
2. `assets/usb-msc-covers/stations/<id|uid|slug>.jpg`  
3. generiertes Text-Cover (Pillow)

---

## Lab-Verifikation (Auszug 2026-09-18)

| Check | Ergebnis |
|-------|----------|
| Root-Paging → Audio/Verbindungen/System | OK |
| Quellen → Bibliothek über Mehr | OK |
| Webradio Stream + APIC generated | OK |
| Library Stream + APIC **embedded** | OK |
| Spotify nur Activate | OK |
| DAB/FM | übersprungen (keine Antenne) |

```bash
curl -o /tmp/s.mp3 http://<ESP>/api/lab/stream
ffprobe -show_entries format_tags=title,artist,album /tmp/s.mp3
```

---

## Firmware-Artefakte

| Datei | Nutzung |
|-------|---------|
| `dist/pidrive.0.4.3-dev.usb.esp32s3.bin` | Hub-/USB-Flash @ `0x0` |
| `dist/pidrive.0.4.3-dev.ota.esp32s3.bin` | SoftAP `/ota-upload` / Hub-OTA |

Hub: [`iobroker.esp-hub/firmware/`](https://github.com/MPunktBPunkt/iobroker.esp-hub/tree/main/firmware).
