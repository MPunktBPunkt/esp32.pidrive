# Auftrag L0 — FAT16 Geometrie (Lab-first)

**Stand:** 2026-10-03 · aktiv  
**FW-Ziel:** `0.4.37-dev` mit `PIDRIVE_GEO_L0`  
**Bezug:** pidrive [`AUFTRAG-MSC-HOST-READ-NACHWEIS`](https://github.com/MPunktBPunkt/pidrive/blob/main/docs/auftraege/AUFTRAG-MSC-HOST-READ-NACHWEIS.md) M2  
**Auto `.89`:** bleibt auf **0.4.36** bis B7 durch ist — **kein** Auto-OTA

## Ziel

Virtuellen Stick so vergrößern, dass L3/L4 (8–50 MiB) später möglich sind. Erster Lab-Schritt: **FAT16, 4 KiB-Cluster, 4 MiB Disk**.

## Build / Flash

```bash
cd ~/projects/esphub/esp32.pidrive
pio run -e pidrive-s3-l0
# OTA nur Lab:
curl -sS -F "file=@dist/pidrive.0.4.37-dev.ota.esp32s3.bin" http://192.168.178.88/ota-upload
curl -s http://192.168.178.88/api/status | jq '{v:.version, sectors:.msc.sectorCount, ready:.msc.ready}'
```

## Lab-Smoke

1. `version` = `0.4.37-dev`, `sectorCount` = **8192**  
2. SoftAP/Listing: STATIONS sichtbar (Remount falls nötig)  
3. Optional: Linux-Host `/dev/sg0` Capacity = 8192×512  
4. **Nicht** auf `.89` flashen

## Danach

- L0_16M Profil (16 MiB)  
- Statische Zeitmarker-MP3 für L-Leiter (M3)  
- Auto-OTA erst nach B7
