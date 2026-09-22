# Offene Punkte — `esp32.pidrive`

**Stand:** 2026-09-17

---

## Owner-Fragen

| ID | Frage | Tendenz | Status |
|----|--------|---------|--------|
| **Q-USB-1** | USB neben BT? | **neben** | □ Owner-OK |
| **Q-USB-2** | Encode Pi oder S3? | **Pi → MP3** | □ |
| **Q-USB-3** | PUMP-Transport V1? | **UART/CDC** | ☑ Lab 0.3.x (WLAN = V1.1) |
| **Q-USB-4** | V1 nur flache Stationsliste? | ja · max. 4 Slots + Navigation | ☑ Lab |
| **Q-USB-5** | Hub-OTA Tag 1? | **ja** (Familie) | □ bestätigt |
| **Q-USB-6** | Max. Umschaltzeit? | messen | □ |
| **Q-USB-7** | Dension als Messgerät? | optional | □ |

---

## Entscheidungen (A)

| ID | Thema | Status |
|----|--------|--------|
| **A1** | ESP-IDF (nicht Arduino-App) | Tendenz bestätigt |
| **A-HUB** | Hub USB+OTA Pflicht, `fwType=pidrive`, `chipModel=esp32s3` | Tendenz bestätigt |
| **A-CHIP** | ESP32-S3 | bestätigt |
| **A-PAR** | Parallel `audio_output=bt` \| `usb_gadget` | Tendenz bestätigt |

---

## Risiken (Auszug)

| ID | Risiko | Mitigation |
|----|--------|------------|
| R1 | Lab ≠ NBT Evo | G-USB-0 Stick-Spike |
| R2 | Prefetch-False-Activate | Observe-first |
| R3 | Hub-Familien-Sperre falscher Bin-Name | Artefakt `*.esp32s3.bin` |
| R4 | OTG+Serial-JTAG PHY-Konflikt | UART-Bridge-Buchse für PUMP |
| R5 | STREAMING+OTA | Defer wie bt-gateway |

---

## Empfohlene Reihenfolge

```
0. Lab L1–L3 MSC-Skeleton          ✓
1. HubClient + Merged-Flash @0x0   ✓
2. Statische MP3 über MSC          ✓ (Demo)
3. PUMP UART Menü + Activate       ✓ 0.3.1-dev Lab
3b. Audio USB-Messung                ✓ Negativ (Demo-FAT only) — 2026-09-17
4. PUMP Live-MP3 + Pi-Client       ✓ 0.4.0–0.4.2 Lab
4b. Soft-Paging + embedded APIC      ✓ 0.4.3 Lab — 2026-09-18
5. USB-Host spielt Stick-Datei      ← next (FAT-Patch Feldtest)
6. G-USB-0 Fahrzeug (pidrive) — **Play-Detection I0 in 0.4.14**; Feld-A/B offen
7. PiDrive audio_output=usb_gadget
```

**2026-09-22:** Problem B Instrumentierung — siehe [PLAY-DETECTION.md](PLAY-DETECTION.md).
