# NBT-Replay Suite Dashboard — 0.4.36-dev — 20261001-210858

Mode: `sg` · ESP: `http://192.168.178.88`

| Scenario | Overall | reboot | preΔ | streamΔ | underrunΔ | live_ratio | read_bytes | report |
|----------|---------|--------|------|---------|-----------|------------|------------|--------|
| `burst_then_quiet` | **WARN** | False | 0 | 0 | 0 | None | 16384 | burst_then_quiet__0.4.36-dev__20261001-210858.md |
| `head_reread_after_quiet` | **WARN** | False | 0 | 0 | 0 | None | 16384 | head_reread_after_quiet__0.4.36-dev__20261001-210858.md |
| `sequential_past_head` | **WARN** | False | 0 | 0 | 0 | None | 16384 | sequential_past_head__0.4.36-dev__20261001-210858.md |
| `prefetch_then_warm` | **WARN** | False | 0 | 180224 | 128656 | 0.286 | 180224 | prefetch_then_warm__0.4.36-dev__20261001-210858.md |

## B6 verdicts (`prefetch_then_warm`)

- `pre_warm_bytes`: **PASS**
- `stream_after_arm`: **PASS**
- `overlay_live`: **WARN**

