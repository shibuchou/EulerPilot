# Redis sched_ext 正式对照

- timestamp: 2026-07-24T14:09:06+08:00
- runs: 3
- redis port: 6386
- bench clients: 16
- bench requests: 20000
- stress workers: 4
- scx bin: /usr/local/bin/scx_eulerpilot

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

口径说明：

- `cpu_per_10k_requests` 来自同窗口全系统 `/proc/stat`，对应 env 字段为 `cpu_metric_scope=system_proc_stat`，只作为辅助效率指标，不作为目标 cgroup CPU 消耗结论。
- `noisy_scx_psi` 会额外运行 Redis PSI probe 以稳定触发 PSI gate，适合作为门控/调度路径证据；净性能对比应优先看无额外 probe 的 default/cgroup/scx-normal 组。
