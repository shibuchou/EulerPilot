# Redis sched_ext 正式对照

- timestamp: 2026-06-12T18:52:38+08:00
- runs: 1
- redis port: 6386
- bench clients: 8
- bench requests: 10000
- stress workers: 2
- scx bin: /root/olk/kernel-OLK-6.6-atomgit/tools/sched_ext/build/bin/scx_eulerpilot

本目录包含：

- `run_manifest.json`
- `compare_summary_avg.csv`
- `report.md`
- `run-*/<label>_summary.csv`
- `run-*/<label>_agent_snapshot.txt`
- `run-*/<label>_scx_stats.json`
- `run-*/<label>_gate_status.txt`
