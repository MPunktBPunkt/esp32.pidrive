# NBT-Replay — Lab-USB-Host gegen Feld-/Synthese-Traces

**Stand:** 2026-10-01  
**Auftrag:** [AUFTRAG-NBT-REPLAY-HARNESS.md](../auftraege/AUFTRAG-NBT-REPLAY-HARNESS.md)  
**Tools:** `tools/nbt_replay.py`, `nbt_report.py`, `nbt_suite.py`, `traces/`

---

## Symptom / Warum

Feld und Lab zeigen: Nach einem kurzen MSC-Burst liest der NBT oft **minutenlang nicht**
weiter (Cache). Linux-`usb-storage` liest dagegen aggressiv und kann den ESP rebooten.
Wir brauchen einen Host, der **dieselben Read-Zeitachsen** wie im Feld fährt — inkl. Quiet.

## Komponenten

```
traces/*.replay.json  →  nbt_replay.py (--direct | --sg)
                              │  paralleler /api/status-Sampler
                              ▼
                         raw run JSON  →  nbt_report.py  →  reports/*.md
                              ▲
                         nbt_suite.py (Szenarien 1–3)
```

## Lab-Schnellstart (Proxmox-Host `.108`, ESP2 `.88`)

Suite **nicht** im LXC (dort fehlt `/dev/sda` trotz `lsblk`). Auf dem Host:

```bash
# Device nicht mounten; Bridge für Overlay-Szenarien 2/3
python3 tools/nbt_suite.py \
  --esp http://192.168.178.88 \
  --dev /dev/sda --sg /dev/sg0 \
  --mode sg \
  --settle 60
```

**Schwellwert Lab 2026-10-01:** SG consecutive Reads am Slot ≥ 32 KiB → ESP-Reboot.
Suite-Default-Traces sind auf **≤16 KiB @ ≥150 ms/4 KiB** gekappt. Aggressiver
`b1_burst_quiet.replay.json` (512 KiB) bleibt als Stress-Trace (erwartet FAIL).

Einzelner Lauf:

```bash
python3 tools/nbt_replay.py \
  --trace tools/traces/b1_burst_quiet.replay.json \
  --esp http://192.168.178.88 \
  --sg /dev/sg0 --mode sg \
  --settle 60 \
  --out tools/reports/last_run.json
python3 tools/nbt_report.py tools/reports/last_run.json
```

## Kennzahlen

| Key | Bedeutung |
|-----|-----------|
| `esp_reboot` | Uptime sprang rückwärts / Version-Reset |
| `underrun_delta` | Stream-Underruns während Reads |
| `head_resync_delta` | B4 Head-Remaps |
| `stream_bytes_delta` | Overlay-Bytes an Host |
| `max_read_gap_ms` | längste Pause zwischen Reads |
| `live_ratio` | `(stream_bytes - underruns) / stream_bytes` falls sinnvoll |

## Grenzen

- Replay ≠ Vorhersage von HU-Cache.
- Synthetische Traces V1; echte Feld-LBA-Sequenzen brauchen größeren ESP-Trace-Flush.
- SoftAP-Cursor-Tests (`lab_b4_reread.py`) bleiben die Reset-freie Cursor-Regression.
- **Keine gemeinsame Wanduhr** ESP↔Pi↔Handy bis Trace-Ring+NTP; Feldabend: Sync-Marker-Protokoll (siehe Auftrag).

## Nächste Schritte

1. Feldabend 0.4.31: neuen Trace sammeln → `traces/feldtest-2026-10-01*.replay.json`
2. Trace-Ring >96 Samples + optional jsonl pro Read
3. Szenario 4 paced_realtime, Geometrie später
