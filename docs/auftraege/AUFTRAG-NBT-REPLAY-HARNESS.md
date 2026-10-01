# Auftrag: NBT-Replay-Harness — reproduzierbarer BMW-USB-Host-Test (ESP2 Lab)

**Stand:** 2026-10-01 · aktiv  
**Repo:** `esp32.pidrive` (`tools/nbt_*.py`, `tools/traces/`)  
**Feldbericht:** [pidrive FELDTEST-ESP-MSC-BMW-2026-09-28](https://github.com/MPunktBPunkt/pidrive/blob/main/docs/betrieb/FELDTEST-ESP-MSC-BMW-2026-09-28.md) §11.4 / §15.2  
**Planung:** [NBT-REPLAY.md](../planung/NBT-REPLAY.md)

---

## Ziel

Werkzeug auf dem Lab-Host, das *aufgezeichnetes oder synthetisiertes* USB-Leseverhalten
des BMW NBT gegen ESP2 (`.88`) abspielt — dauerhafte Regression (0.4.32+), bevor ein
Auto-Termin nötig ist.

## Nicht-Ziele

- Den BMW / HU-Cache **emulieren** oder vorhersagen („liest er erneut?“ = Auto-exklusiv).
- SoftAP-`overlay_read` ersetzen (bleibt parallel für Cursor ohne USB-Reset).
- Geometrie-Matrix und `pidrivectl`-Hook in Phase 1.

## Hardware / Fallen

| | |
|--|--|
| Lab | Proxmox Host ↔ ESP2 `192.168.178.88`, FW ≥ 0.4.31 |
| Devices | `/dev/sda` (Block) + `/dev/sg0` (SG_IO) — **nicht mounten** |
| Falle | Aggressiver `usb-storage`-Burst → ESP USB-Reset/Reboot → Harness steuert Reads selbst |

---

## Scope-Schärfung (Review 2026-10-01)

1. **Exporter aus Artefakten 28.09** liefert **kein** volles LBA-Timing (Events/Diag ohne
   kompletten Trace-Ring; ESP-Trace nur 96 Samples). Abnahme „28.09-jsonl → Replay“ ist
   **Phase‑2+** nach größerem Trace-Flush.
2. **V1-Traces = synthetisch** aus dokumentierten Mustern (B1, Feld 10-01).
3. Suite/Dashboard/Geometrie/`pidrivectl` = später (Phase 4–5).
4. Harness-Entwicklung nur gegen `.88`; Auto `.89` unberührt.
5. Keine Overlay/Cursor-FW parallel zum Harness-Bau (Variable trennen) — Ausnahme:
   Trace-Ring vergrößern erst nach 0.4.31-Feldabend, wenn Bibliothek echt werden soll.

---

## Komponenten (`tools/`)

| Tool | Rolle | Phase |
|------|--------|-------|
| `traces/*.replay.json` | Trace-Bibliothek | 1 synthetisch |
| `nbt_replay.py` | Timinggetreue Reads + Status-Sampler | 1 |
| `nbt_report.py` | Kennzahlen + PASS/WARN/FAIL | 1 |
| `nbt_suite.py` | Szenarien 1–3 | 1 |
| `nbt_trace_export.py` | Diag/Events → Replay (best effort) + Synthese-Hilfen | 2 |
| `reports/` | Lauf-Reports (gitignored Raw optional) | 1 |

### Replay-Format

```json
{
  "meta": {"name": "b1_burst_quiet", "source": "synthetic-B1", "fw_ref": "lab"},
  "geometry": {"slot0_lba": 57, "sector": 512},
  "events": [
    {"t_ms": 1000, "op": "read", "lba": 57, "bytes": 4096},
    {"t_ms": 2000, "op": "quiet", "duration_ms": 180000}
  ]
}
```

`t_ms` = ms seit Harness-Start (nach Device-Ready), nicht seit ESP-Boot.

### Transport

1. **O_DIRECT** — `os.open(O_RDONLY|O_DIRECT)` + aligned `pread` (schnell lauffähig).
2. **SG_IO** — READ(10) auf `/dev/sg*` (Primärziel, gleiche CDB-Ebene, kein Page-Cache).

Warnung wenn Blockgerät gemountet.

### Sampler

Thread, ~1 s: `GET /api/status` → Zeitreihe (`cursorArmed`, `hostAbsCursor`, `underruns`,
`streamBytes`, `phase`, `uptime`, `version`, …). Marker vor/nach Events.

---

## Szenarien (V1)

1. **burst_then_quiet** — Trace-Burst, dann `--settle` (Default 60 s Lab / 180 s Feld-ähnlich).  
   Assert: kein ESP-Reboot (Uptime-Sprung), Overlay/Cursor nicht zwangs-reset.
2. **head_reread_after_quiet** — nach Quiet Read am Slot-Head.  
   Assert: `headResyncs+`, `underrunΔ≈0` (wenn Stream warm).
3. **sequential_past_head** — Offsets 4 KiB…80 KiB.  
   Assert: `live_ratio` hoch / `underrunΔ` klein (bei warmem Overlay).

4/5 (paced_realtime, geometry_matrix) — später.

---

## Lab-Ergebnis 2026-10-01 (ESP2 `.88`, FW 0.4.31-dev, Proxmox SG_IO)

| Trace | Ergebnis |
|-------|----------|
| SG 16 KiB @ 80–150 ms/4 KiB | **PASS** (kein Reboot) |
| SG ≥32 KiB consecutive slot0 | **FAIL** — ESP USB-Reset/Reboot (auch @150 ms) |
| Full B1 512 KiB @7–25 ms | **FAIL** Reboot (harness erkennt Uptime-Sprung) |
| Suite Default = `b1_burst_quiet_gentle` (16 KiB) | Abnahme-Smoke |

`O_DIRECT` bleibt implementiert; nach USB-Reset oft IO-Errors bis Re-Enum. Suite auf **Proxmox-Host** (`.108`), nicht im LXC (kein `/dev/sda`).

## Abnahme V1

- [x] Auftrag + `NBT-REPLAY.md` + synthetische Traces
- [x] `nbt_replay.py --sg` Szenario 1 **gentle** gegen `.88` ohne ESP-Reboot (16 KiB)
- [ ] `nbt_replay.py --direct` Szenario 1 ohne Reboot (nach frischem Enum; oft IO nach Reset)
- [x] Szenario 2/3 Report PASS/WARN dokumentiert (Stream/Bridge; Traces auf ≤16 KiB gekappt)
- [x] `nbt_suite.py` erzeugt Dashboard (Proxmox `/tmp/nbt_harness/reports/` + `tools/reports/baseline-20261001/`)
- [x] Push auf GitHub

## Abnahme später

- [ ] Exporter aus echtem Feld-Trace (nach Trace-Ring-Upgrade)
- [ ] Feld-Traces 10-01 Abend in `traces/`
- [ ] `pidrivectl test nbt-replay` Hook

---

## Reihenfolge

1. Synthetische Traces + Replay O_DIRECT + Sampler + Report  
2. SG_IO-Mode  
3. Suite Szenarien 1–3  
4. Trace-Export / Ring-Upgrade nach Feldabend  
5. Doku-Feinschliff + optional pidrivectl
