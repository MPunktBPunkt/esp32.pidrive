# Auftrag L0 / L-Leiter-Geometrie (Lab)

**Stand:** 2026-10-03 · Lab aktiv  
**Bezug:** pidrive [`AUFTRAG-MSC-HOST-READ-NACHWEIS`](https://github.com/MPunktBPunkt/pidrive/blob/main/docs/auftraege/AUFTRAG-MSC-HOST-READ-NACHWEIS.md) M2/M3  
**Session:** pidrive `docs/betrieb/artifacts-2026-10-03-m3/SESSION-LAB-2026-10-03.md`

## Envs

| Env | FW | Geometrie | Hinweis |
|-----|-----|-----------|---------|
| `pidrive-s3-l0` | 0.4.40 | FAT12 4 MiB / 1 MiB-Slots | Clusterzahl &lt;4085 → FAT12 |
| `pidrive-s3-l0-16m` | 0.4.41 | FAT16 16 MiB / 3×4 MiB | Host-verifiziert FAT16 |
| `pidrive-s3-l3` | **0.4.42** | FAT16 16 MiB / **fav0=8 MiB** + 2×512 KiB | L3-Messslot |

```bash
pio run -e pidrive-s3-l3
curl -sS -F "firmware=@dist/pidrive.0.4.42-dev.ota.esp32s3.bin" http://192.168.178.88/ota-upload
```

## Lab-Smoke Checkliste

1. `version` + `sectorCount=32768` (16 MiB-Profile)  
2. `msc.ready` Detail-String (`FAT16 L3…` / `FAT16 L0 16MiB…`)  
3. Host: `file -s /dev/sda` enthält `FAT (16 bit)`  
4. Slot-MiB aus Status `slotMap`  
5. Stream aus → `m3_lab_verify_markers.py`  
6. Optional: timeline + marker-pace  

**Auto `.89`:** nicht mit Lab-L3 flashen, bis Feldplan das vorsieht.
