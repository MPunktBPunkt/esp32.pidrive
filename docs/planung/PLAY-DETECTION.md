# Play-Detection (Problem B)

**Stand:** 2026-09-28 · Firmware **0.4.17-dev**  
**Auftrag:** [pidrive AUFTRAG-ESP-PLAY-DETECTION](https://github.com/MPunktBPunkt/pidrive/blob/main/docs/auftraege/AUFTRAG-ESP-PLAY-DETECTION.md)

## Symptom

BMW spielt Demo-/Stub-MP3s; Live-Stream startet oft nicht, weil `looksLikePlay` kein `play.guess` feuert.  
Zusätzlich: iDrive kann sich „anders als erwartet“ verhalten (Cache, Index-Fenster, kein Re-Read vom Dateianfang).

## I0 (eingebaut, ab 0.4.14)

| Signal | Bedeutung |
|--------|-----------|
| `play.reject` | Reason + `uid` + `from=` LBA + `+NB` + `age=` seit Plug |
| Reasons | `plug_window`, `not_from_head`, `seq_short`, `prefetch`, `cooldown`, `already_playing` |
| Status JSON | `msc.playDetect`, `playRejectCount`, `playGuessCount` |

### Config (SoftAP → Config-Tab / `/api/config`)

| Key | Default | A/B-Idee |
|-----|---------|----------|
| `playPlugWindowMs` | 2500 | → 500 oder 0 |
| `playMinSeqBytes` | 6000 | → 2048 |
| `playHeadLbaSlop` | 12 | — |
| `playCooldownMs` | 5000 | — |
| `playPrefetchLbaSlop` | 2 | — |

Änderungen greifen **ohne Reboot** (nach Speichern).

## I0.1 Host-Analyse (ab 0.4.17)

Ziel: beim nächsten Autotest **sehen, was der NBT wirklich tut** — nicht nur die Play-Heuristik.

| Signal | Was es verrät |
|--------|----------------|
| `msc.host` / SCSI-Zähler | Host-Stack: INQUIRY, CAPACITY, TUR, PREVENT, unbekannte Opcodes + Timing seit Plug · Hint `hu-like` / `poll-heavy` / `reprobe` |
| `msc.phase` | `scan` → `index` → `play` / `quiet` |
| `msc.quiet` | Nach Datei-Reads ≥2 s Stille **ohne** `play.guess` → starker Hinweis auf **Cache-Playback** (Demo hörbar, kein Live-Overlay) |
| LBA-Tags | `BOOT` / `FAT0` / `FAT1` / `DIR` / Slotname — Filesystem-Scan vs. Decode |
| `mscTrace[].gap` | Inter-Read-Abstand (Buffering vs. Browse) |
| `xfer` Buckets | 512 / 2k / 4k / 8k+ — typische HU-Transfergröße |
| `slotMap[].fromHead/midFile/maxSeq` | Welcher Stub wie gelesen wurde |

SoftAP Auto-Test zeigt Host-SCSI, Slot-Zugriff und erweiterten Trace (96 Samples).

**PUMP → PiDrive-Log (0.4.17):** ESP sendet `{"t":"event","op":"diag","code":…,"detail":…}` für  
`play.reject` · `msc.phase` · `msc.quiet` · `msc.first_read`.  
`pump_bridge.py` loggt `[msc] …` und hängt an `/tmp/pidrive_msc_diag.jsonl`.  
Zusätzlich pollt `usb_pump_client` Phase/Reject/Host-Hint in `/tmp/pidrive_usb_status.json`.

**Nicht messbar vom Device:** Host-OS-String / USB-Host-VID (Gerät sieht den Host nicht als USB-Device). Fingerprint kommt aus **SCSI-Mix + LBA-Muster**.

## Bridge

`pump_bridge.py` loggt `[trace] t=…ms play_uid=…` und `audio.start` / `audio_start sent`.

## Feldtest-Checkliste

1. SoftAP Events clear → ESP an BMW stecken  
2. Listing abwarten → Phase `scan`/`index` beobachten  
3. Titel antippen → erwarten: entweder `play.guess` **oder** `msc.quiet` / `play.reject` mit Reason  
4. Ein Config-Parameter pro Versuch (I1)  
5. Status JSON / Screenshot Events + Host-Hint sichern  

## Nächster Schritt

Feldtest I1 mit Host-Phase + `msc.quiet` als Primärsignal neben `play.reject`.
