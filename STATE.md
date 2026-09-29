# STATE — esp32.pidrive

Lebender Projektstand. Kurz halten; Details in `docs/planung/` und Feldbericht.

| Feld | Wert |
|------|------|
| Stand | 2026-09-29 |
| Phase | **Firmware 0.4.26-dev** — Nav Play-Detect (action/folder) + Serial-Bump Unplug |
| Repo | `MPunktBPunkt/esp32.pidrive` |
| Build | PlatformIO `env:pidrive-s3` (`pio run`) |
| Dist | `dist/pidrive.0.4.26-dev.ota.esp32s3.bin` (nach Build) |
| Hub-Depot | `iobroker.esp-hub/firmware/pidrive.0.4.26-dev.*.esp32s3.bin` |
| Hardware | ESP32-S3-DevKitC-1 (OTG + UART) |
| Pi-Link | [PUMP.md](docs/planung/PUMP.md) · Bridge `tools/pump_bridge.py` |
| Feldbericht | [FELDTEST-ESP-MSC-BMW-2026-09-28](https://github.com/MPunktBPunkt/pidrive/blob/main/docs/betrieb/FELDTEST-ESP-MSC-BMW-2026-09-28.md) |
| Auftrag | [AUFTRAG-ESP-PLAY-DETECTION](https://github.com/MPunktBPunkt/pidrive/blob/main/docs/auftraege/AUFTRAG-ESP-PLAY-DETECTION.md) |

## Aktueller Fokus

1. **P0 Baustelle A:** Nav Play-Detect 0.4.26 (Feld: Zurueck oft `seq_short`/`cooldown`) + Bridge Grace — nächster Autotest Pass A/B/C
2. **P0 Baustelle B:** Live-Audio vs. HU-Cache (Silence+Xing, Pacing, Pulse-Messung) — erst nach A grün
3. Feld: SoftAP-direkt/UART bevorzugen bei schlechtem STA-RSSI

## Letzte Änderung

- 2026-09-29: **0.4.26-dev** Nav-Slots (`action`/`folder`/`pump:*`): `navMinSeqBytes=4096`; Cooldown blockiert keine Navigation; `kind` in slotMap; Serial-Bump Unplug bleibt (0.4.25)
- 2026-09-28: **0.4.25-dev** `applyUsbIdentity()` auch bei Unplug; Bridge: UID-Grace 180 s + menu snapshot resent
- 2026-09-28: **0.4.24-dev** Remount: USB `serialNumber` `PDnnnn` + Volume-Label + 600 ms Hide; `POST /api/lab/remount`
- 2026-09-28: **0.4.23–0.4.18** siehe Git-Log / älteren STATE
