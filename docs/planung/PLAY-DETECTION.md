# Play-Detection (Problem B)

**Stand:** 2026-09-22 · Firmware **0.4.14-dev**  
**Auftrag:** [pidrive AUFTRAG-ESP-PLAY-DETECTION](https://github.com/MPunktBPunkt/pidrive/blob/main/docs/auftraege/AUFTRAG-ESP-PLAY-DETECTION.md)

## Symptom

BMW spielt Demo-/Stub-MP3s; Live-Stream startet oft nicht, weil `looksLikePlay` kein `play.guess` feuert.

## I0 (eingebaut)

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

## Bridge

`pump_bridge.py` loggt `[trace] t=…ms play_uid=…` und `audio.start` / `audio_start sent`.

## Nächster Schritt

Feldtest I1: ein Parameter pro Versuch; Events-Tab am SoftAP beobachten.
