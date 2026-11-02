# Lab-Bericht: Cross-Check mit esp32.usb-analyzer (Bugs, Lücken, Messungen)

| | |
|--|--|
| **Datum** | 2026-11-02 |
| **esp32.pidrive** | Build **pidrive-s3-l3** · `FW_VERSION` 0.4.60-dev · Board `.89` |
| **Gegenstelle** | [esp32.usb-analyzer](https://github.com/MPunktBPunkt/esp32.usb-analyzer) Host `.88` **0.0.48–0.0.49** |
| **Topologie** | Analyzer-Host OTG ↔ USB-C ↔ PiDrive MSC-Gadget |
| **Evidence (Analyzer)** | `docs/evidence/LAB_PIDRIVE_FOREIGN_DUT_*` · `LAB_BMW_HU_STIMULUS_PIDRIVE_*` |

**Claim-Klassen:** FACT / MEASURED / INFERENCE · **Not claimed:** echte BMW-NBT · HU-Cache-Vorhersage · L0-Wire.

---

## 0. Ein-Satz-Urteil

Gegen den Analyzer-Host ist der MSC-Stack **instrumentiert und nicht blind** (Counter matchen Host-Bytes); gleichzeitig fallen **Status-/Identitäts-Bugs** auf (`imageBytes`, `version`, USB-Produktstring) und der ID3-Head im Silence-Pfad ist nur minimal (`TSSE`).

---

## 1. Methode

Analyzer-Host erzeugt kontrollierte Zugriffe:

1. Fremd-DUT-Regression (Enum, Capacity, Boot, File-Head ID3, Scan-Head).  
2. BMW-ähnlicher Stimulus (`bmw_mount_scan`): Desk P-MOUNT/P-MREAD + 160-Block-Scan ab fav0 LBA 81.

Frage: Sieht pidrive dieselben Zugriffe in `msc` / `host`-Countern?

---

## 2. Bugs / Defekte (MEASURED)

### B1 — `imageBytes` falsch bei L3-Geometrie

| Feld | Wert | Erwartung |
|------|------|-----------|
| `msc.sectorCount` | **32768** | 16 MiB ✓ |
| `msc.huContract.geo` | `FAT16 L3 16MiB fav0=8M` | ✓ |
| `msc.imageBytes` | **262144** (256 KiB) | **16777216** |

Ursache (Code): Status meldet `DEMO_FAT_SIZE` statt L3-`kVirtSectorCount * 512` (`UsbMscGadget.cpp`).

**Impact:** Hub/UI/Telemetrie zeigen falsche Mediengröße → Verwechslung Demo vs. L3.

### B2 — `/api/status` → `version: "0.0.0"`

Build-Flag `FW_VERSION=0.4.60-dev`, Serial-Boot loggt korrekt; HTTP-Status lieferte **`0.0.0`**.  
→ Reporting/Feld-Mapping prüfen (welches JSON-Feld Hub/UI liest).

### B3 — USB-Produktstring ≠ SCSI-Identität

| Ebene | MEASURED |
|-------|----------|
| Host DUT descriptors | manufacturer Espressif · product **DevKitC-1-N8…** · serial `PD0332` |
| SCSI INQUIRY | vendor **`PIDRIVE`** · product **`USB_MEDIA`** · rev `0332` |

**Impact:** Hosts/HU, die Strings statt Inquiry nutzen, sehen „Espressif DevKit“, nicht PiDrive.

### B4 — SoftAP `softap.fail` bei STA OK

Events `softap.fail` während Lab-STA `.89` erreichbar. Nebenbei, aber Lab-Rauschen.

---

## 3. Messungen — Cross-Check grün

### M1 — Fremd-DUT MSC / FAT / ID3

| Check | Result |
|-------|--------|
| Capacity | 32768 × 512 = 16 MiB |
| Boot | `mkfs.fat` · FAT16 · sig `55aa` |
| fav0/fav1 Head | magic **`ID3`** · v2.4 · Frame **`TSSE`** / `Lavf61.7.103` (+ Xing/Info) |
| Lab-Pattern-Probe | `pattern_ok=false` (erwartet — kein Analyzer-Pattern) |
| HU-like Scan 160 Blöcke @ LBA 81 | 81920 B OK · `playGuessCount=1` · hint `poll-heavy` |

### M2 — BMW-HU-like Stimulus (`bmw_mount_scan`)

Host 0.0.49 · tur=32 · scan_blocks=160 · file_lba0=81 · Dauer **174 ms**:

| Host | DUT delta |
|------|-----------|
| inquiryOk=1 · capacityOk=3 · turOk=32 | inquiry=1 · capacity=3 · tur=66 · hint `poll-heavy` |
| readOk=31 · **109056 B** | **readCount +31** · **bytesRead +109056** (byte-exact) |
| | bytesBoot +12800 · bytesFat +14336 · **bytesFile +81920** |

→ **Nicht blind:** kontrollierter Host-Traffic erscheint 1:1 in den Gadget-Countern.

**Nicht emittiert (Analyzer-Limit):** MODE_SENSE6 · PREVENT_ALLOW.

---

## 4. Lücken (kein Crash, aber unvollständig)

| ID | Thema | Hinweis |
|----|--------|---------|
| L1 | ID3 nur `TSSE` (~44 B), kein TIT2/TPE1/APIC | Silence-/Idle-Head; reicher Sticky-ID3 braucht aktiven Stream/PUMP |
| L2 | `bytesDir=0` trotz Mount-Reads | Klassifikation ROOT vs DIR / Host traf kaum DIR-LBAs |
| L3 | `playGuess` nur bei Scan, nicht bei jedem Stimulus | Heuristik-Parameter — kein Counter-Bug |
| L4 | NBT-Replay Timing | Analyzer-Stimulus ≈ Mount+Scan, kein voller Feld-Trace-Replay |

---

## 5. Empfohlene Maßnahmen

1. **P0:** B1 `imageBytes` an echte Geometrie koppeln + Assert in Lab.  
2. **P0:** B2 Status-`version` = `FW_VERSION`.  
3. **P1:** B3 USB String-Deskriptoren an PiDrive-Branding (serial/`PD####` ok).  
4. Lab: Analyzer-Stimulus in CI/Smoke (`lab_bmw_hu_stimulus.py --dut pidrive`).  
5. Optional: aktiver Stream + APIC-Head gegen Analyzer-Host lesen.

---

## 6. Verweise

- Analyzer: https://github.com/MPunktBPunkt/esp32.usb-analyzer  
  - `tools/lab_bmw_hu_stimulus.py` · Evidence `LAB_BMW_HU_STIMULUS_PIDRIVE_*` · `LAB_PIDRIVE_FOREIGN_DUT_*`  
- NBT-Replay (bestehend): [`docs/planung/NBT-REPLAY.md`](../planung/NBT-REPLAY.md)  
- Schwesterbericht hu-probe: `docs/feld/2026-11-02-lab-usb-analyzer-crosscheck.md`  
- PiDrive (Pi) Kurzverweis: `docs/betrieb/2026-11-02-esp-usb-analyzer-crosscheck.md`

---

## 7. Fix verification · 0.4.61-dev (same day)

| Bug | After OTA L3 `.89` |
|-----|--------------------|
| B1 `imageBytes` | **16777216** (= 32768×512); `demoImageBytes` keeps 262144 |
| B2 `version` | **`0.4.61-dev`** (also `fwVersion`) |
| B3 USB product string | Host DUT product **`USB Media`** (was DevKitC…) |

BMW stimulus `bmw_mount_scan` still byte-exact vs gadget counters (Analyzer 0.0.50).
