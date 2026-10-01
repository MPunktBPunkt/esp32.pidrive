# Auftrag: `msc.reads`-Streaming — echte LBA-Zeitachsen (nach Feldabend)

**Stand:** 2026-10-01 · geplant (nach 0.4.31-Feld)  
**Repo:** `esp32.pidrive` + Pi `pump_bridge.py` / `nbt_trace_export.py`  
**Vorläufer:** [AUFTRAG-NBT-REPLAY-HARNESS](AUFTRAG-NBT-REPLAY-HARNESS.md) · Ring heute `kTraceSize=96`  
**Feldbericht:** pidrive FELDTEST §11.4 / §15

---

## Ziel

Dauerhaft und verlustarm die **Host-Read-Zeitachse** vom ESP zum Pi bringen, sodass
`nbt_trace_export.py` echte BMW-LBA-Traces (nicht nur Synthese) erzeugt.

## Warum jetzt

- Replay-Harness Phase 1 läuft, aber Bibliothek = synthetisch.
- 96-Sample-Ring + Status-Poll reicht nicht für volle Burst→Quiet-Abende.
- Lab 10-01: SG ≥32 KiB consecutive rebootet ESP — Format/Rate müssen USB-IRQ-sicher sein.

## Nicht-Ziele

- Kein HU-Cache-Emulator.
- Kein Umbau parallel zum **heutigen** 0.4.31-Ohr-Test (erst Beobachtung, dann Dimensionierung).
- Kein Ersatz für Sync-Marker-Protokoll am Feldabend (bleibt bis NTP/`boot_id`).

---

## Scope (geschärft)

### 1. ESP — Drain außerhalb MSC-Callback

- Im `loop()` (oder dedizierter Task mit niedriger Prio): Ring drainen → gebündelte
  `msc.reads`-Events über bestehenden PUMP-TCP.
- **Nie** TCP/JSON im TinyUSB-Read-Callback.
- Payload: mind. `ms`/`gapMs`/`lba`/`bytes`/`kind` (+ optional `tag`); Zeitbasis wie Auftrag
  NBT-Replay (Relativ + später Epoch/`boot_id`).

### 2. Overflow + Ring als Fallback

- `overflowCount` (verworfene Samples seit letztem Flush).
- Ring bleibt Snapshot für SoftAP/`/api/status` wenn TCP weg ist.
- Ziel: bei typischem NBT-Burst **overflow≈0**; Lab-Burst darf overflow melden statt ESP zu killen.

### 3. Pi — jsonl + Exporter

- `pump_bridge.py`: `msc.reads`-Batches → `/tmp/pidrive_msc_reads.jsonl` (neben diag).
- `nbt_trace_export.py`: jsonl → `traces/*.replay.json` inkl. Sync-Anker in `meta`.

### 4. Abnahme-Pfad

1. Lab `.88` + `nbt_suite.py` (Reboot-Check, gentle traces).  
2. Dann OTA `.89` / Feld.  
3. Erst danach synthetische Traces durch Feld-Exports ersetzen.

### 5. Event-Format — erst nach heutigem Ohr-Abend dimensionieren

Beobachtung am BMW steuert Aggregation:

| Muster | Format-Tendenz |
|--------|----------------|
| wenige große Reads | 1 Event / Read ok |
| viele 4 KiB eng | **Burst-Aggregation** (`n`, `bytes_sum`, `gap_first`, `lba_first`…`lba_last`) |
| nur Head, dann Quiet | kurze Reads + explizites `quiet`-Intervall |
| gleichmäßig realtime | paced Samples, Rate-Limit im Drain |

**Heute Abend notieren:** Chunk-Größe, Gaps, Burst-Länge, Quiet-Dauer, Head-only ja/nein.

---

## Abnahme

- [ ] Drain-Task: Callback bleibt frei von Netz-I/O
- [ ] `overflowCount` + Ring-Fallback im Status
- [ ] Bridge schreibt `msc_reads.jsonl`
- [ ] Exporter → Replay; Suite auf `.88` ohne ESP-Reboot (gentle)
- [ ] Ein echter Feld-Trace in `tools/traces/`
- [ ] OTA `.89` erst nach Lab-grün

## Reihenfolge

1. Feldabend 0.4.31 — Ohr + Read-Muster notieren (kein FW-Spike)  
2. Format festnageln (pro Read vs. Burst)  
3. ESP Drain + PUMP Events  
4. Bridge jsonl + Exporter  
5. Lab Suite → Feld-OTA
