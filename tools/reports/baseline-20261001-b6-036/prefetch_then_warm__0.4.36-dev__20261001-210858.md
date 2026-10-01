# NBT-Replay Report — feld_prefetch_then_warm_gentle

**Overall:** WARN  
**Transport:** `sg:/dev/sg0`  
**FW:** 0.4.36-dev → 0.4.36-dev  

## Kennzahlen

| Key | Value |
|-----|-------|
| `read_count` | 44 |
| `read_bytes_total` | 180224 |
| `first_read_ms` | 2000 |
| `last_read_ms` | 8450 |
| `max_read_gap_ms` | 150 |
| `underrun_delta` | 128656 |
| `stream_bytes_delta` | 180224 |
| `pre_warm_delta` | 0 |
| `live_ratio` | 0.286 |
| `head_resync_delta` | 1 |
| `esp_reboot` | False |

## Verdicts

- **PASS** `esp_reboot` — no uptime regression
- **PASS** `pre_warm_bytes` — preΔ=0 (target 0 @ warmup=0)
- **PASS** `stream_after_arm` — streamBytesΔ=180224 (live path served prefetch)
- **WARN** `overlay_live` — live_ratio=0.286 (underrun silence; ring prefill next)
- **PASS** `head_resync` — headResyncs 0→1
- **PASS** `io_errors` — 0 IO errors
