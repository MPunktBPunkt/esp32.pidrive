# STATE — esp32.pidrive

Lebender Projektstand. Kurz halten; Details in `docs/planung/` und pidrive-Feldberichten.

| Feld | Wert |
|------|------|
| Stand | 2026-10-24 |
| Phase | **0.4.59-dev** — Lead-BUSY ohne Underrun + `lab/tone_smoke`; Lab-Smoke PASS |
| Repo | `MPunktBPunkt/esp32.pidrive` |
| Build | `pio run -e pidrive-s3` (`PIDRIVE_GEO_FULLMENU=1`) |
| Hardware | ESP32-S3-DevKitC-1 **N8R2** (OTG + UART) |
| Pi-Docs | `pidrive/docs/planung/MASSNAHMEN-HU-PROBE-2026-10-23.md` |

## Aktueller Fokus

1. Lab `.88`: Zielbaum per OTA 0.4.57 + Bridge `menu_set` mit `path`.
2. Struktur stabil; nur Body/Live-Overlay ändern; Menü-Namen → `msc.cycle`.
3. Größenleiter ≫160 KiB / Option-b weiter offen.

## Letzte Änderung

- 2026-10-24: **0.4.59-dev** — Lead wartet auf Ring (BUSY+maxMs); `/api/lab/tone_smoke`; `tools/lab_tone_smoke.py` PASS (id3+lead+stall_ok, underrun=0)
- 2026-10-24: **0.4.58-dev** — `leadBytes` uint32 (120 KiB); `msc.tone.*` + DiagLog `[TONE]`; Stall Lab-Default scharf
- 2026-10-24: **0.4.57-dev** — Zielbild-FAT (Root `0ZULETZT` + 7 Quellordner); MenuStore/`path` aus JSON
- 2026-10-23: **0.4.56-dev** — `PIDRIVE_GEO_FULLMENU` (FAT16 8 MiB, 32×160 KiB); multi-sector Dir; bridge ohne Mehr…
- 2026-10-23: **0.4.55-dev** — `requestHostReread` (D+ Cycle); Soft-Remount demoted
- 2026-10-22: **0.4.54-dev** — PSRAM + R13 Mutex + PanicTail/PhaseGate/DiagLog
