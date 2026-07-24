# Nginx sched_ext 正式对照

- timestamp: 2026-07-23T16:47:22+08:00
- runs: 10
- nginx port: 18082
- wrk threads: 2
- wrk connections: 32
- wrk duration: 10s
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

口径说明：

- `cpu_per_10k_requests` 来自同窗口全系统 `/proc/stat`，对应 env 字段为 `cpu_metric_scope=system_proc_stat`，只作为辅助效率指标，不作为目标 cgroup CPU 消耗结论。
- `noisy_scx_psi` 主要用于证明 PSI gate 与 sched_ext 后端可被触发并完成状态切换；Nginx 性能结论按 workload 边界解释，不声明所有场景稳定优于默认调度器。
