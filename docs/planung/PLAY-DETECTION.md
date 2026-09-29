# Play-Detection (Problem B)

**Stand:** 2026-09-29 · Firmware **0.4.26-dev**  
**Auftrag:** [pidrive AUFTRAG-ESP-PLAY-DETECTION](https://github.com/MPunktBPunkt/pidrive/blob/main/docs/auftraege/AUFTRAG-ESP-PLAY-DETECTION.md)

## Symptom

BMW spielt Demo-/Stub-MP3s; Live-Stream startet oft nicht, weil `looksLikePlay` kein `play.guess` feuert.  
Zusätzlich: iDrive kann sich „anders als erwartet“ verhalten (Cache, Index-Fenster, kein Re-Read vom Dateianfang).  
**Feld 2026-09-29:** Menü-Nav (Zurueck) oft `seq_short` (ein 4 KiB-Read) oder `cooldown` nach Auto-Play-Station — behoben in 0.4.26 für `action`/`folder`/`pump:*`.

## I0 (eingebaut, ab 0.4.14)

| Signal | Bedeutung |
|--------|-----------|
| `play.reject` | Reason + `uid` + `from=` LBA + `+NB` + `age=` seit Plug |
| Reasons | `plug_window`, `not_from_head`, `seq_short`, `prefetch`, `cooldown`, `already_playing` |
| Status JSON | `msc.playDetect`, `playRejectCount`, `playGuessCount` |

### Config (SoftAP → Config-Tab / `/api/config`)

| Key | Default | A/B-Idee |
|-----|---------|----------|
| `playPlugWindowMs` | 500 | → 0 |
| `playMinSeqBytes` | 6000 | Stationen / Live |
| `playNavMinSeqBytes` | 4096 | **0.4.26** action/folder nach `indexSettled` |
| `playHeadLbaSlop` | 12 | — |
| `playCooldownMs` | 5000 | gilt **nicht** für Nav-Slots |
| `playPrefetchLbaSlop` | 2 | — |

Änderungen greifen **ohne Reboot** (nach Speichern).

## I0.1 Host-Analyse (ab 0.4.17)

Ziel: beim nächsten Autotest **sehen, was der NBT wirklich tut** — nicht nur die Play-Heuristik.

| Signal | Was es verrät |
|--------|----------------|
| `msc.host` / SCSI-Zähler | Host-Stack + Hint `hu-like` / … |
| `msc.phase` | `scan` → `index` → `play` / `quiet` |
| `msc.quiet` | Cache-Playback-Verdacht |
| `slotMap[].kind` | **0.4.26** station/action/folder |

**PUMP → PiDrive-Log:** diag-Events → Bridge `[msc]` + `/tmp/pidrive_msc_diag.jsonl`.

## Bridge

`pump_bridge.py` loggt `[trace] t=…ms play_uid=…` und `audio.start` / `audio_start sent`.

## Nächster Schritt

Autotest Pass A mit **0.4.26**: nach Quiet gezielt Zurueck — erwarten `play.guess` ohne Lab-Bypass.
