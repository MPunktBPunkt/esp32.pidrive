# NBT-Replay Report — b1_burst_quiet_gentle

**Overall:** PASS  
**Transport:** `sg:/dev/sg0`  
**FW:** 0.4.31-dev → 0.4.31-dev  

## Kennzahlen

| Key | Value |
|-----|-------|
| `read_count` | 4 |
| `read_bytes_total` | 16384 |
| `first_read_ms` | 1500 |
| `last_read_ms` | 1950 |
| `max_read_gap_ms` | 150 |
| `underrun_delta` | 0 |
| `stream_bytes_delta` | 16384 |
| `live_ratio` | 1.000 |
| `head_resync_delta` | 1 |
| `esp_reboot` | False |

## Verdicts

- **PASS** `esp_reboot` — no uptime regression
- **PASS** `overlay_live` — live_ratio=1.000
- **PASS** `head_resync` — headResyncs 0→1
- **PASS** `io_errors` — 0 IO errors
