# Nginx sched_ext 正式对照

- timestamp: 2026-06-12T19:52:03+08:00
- runs: 5
- nginx port: 18082
- wrk threads: 2
- wrk connections: 16
- wrk duration: 5s
- stress workers: 2

本目录包含：

- `run_manifest.json`
- `compare_summary_avg.csv`
- `report.md`
- `run-*/<label>_summary.csv`
- `run-*/<label>_agent_snapshot.txt`
- `run-*/<label>_scx_stats.json`
- `run-*/<label>_gate_status.txt`
- `run-*/run_order.txt`
- `run-*/<label>_invalid_reason.txt`（仅在该组失效时出现）
