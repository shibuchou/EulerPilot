# Redis sched_ext 正式对照

- timestamp: 2026-07-08T15:44:45+08:00
- runs: 3
- redis port: 6386
- bench clients: 16
- bench requests: 20000
- stress workers: 0
- scx bin: /root/olk/kernel-OLK-6.6-atomgit/tools/sched_ext/build/bin/scx_eulerpilot

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
