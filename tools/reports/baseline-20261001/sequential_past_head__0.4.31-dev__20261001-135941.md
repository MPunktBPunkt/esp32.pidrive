# NBT-Replay Report — sequential_past_head

**Overall:** WARN  
**Transport:** `sg:/dev/sg0`  
**FW:** 0.4.31-dev → 0.4.31-dev  

## Kennzahlen

| Key | Value |
|-----|-------|
| `read_count` | 4 |
| `read_bytes_total` | 16384 |
| `first_read_ms` | 2000 |
| `last_read_ms` | 2450 |
| `max_read_gap_ms` | 150 |
| `underrun_delta` | 0 |
| `stream_bytes_delta` | 0 |
| `live_ratio` | None |
| `head_resync_delta` | 0 |
| `esp_reboot` | False |

## Verdicts

- **PASS** `esp_reboot` — no uptime regression
- **WARN** `overlay_live` — no streamBytes growth (overlay idle or silence-only / no bridge)
- **WARN** `head_resync` — headResyncs 1→1
- **PASS** `io_errors` — 0 IO errors
