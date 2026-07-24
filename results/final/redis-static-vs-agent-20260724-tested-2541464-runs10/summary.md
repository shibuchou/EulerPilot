# Redis static vs Agent dynamic compare

- timestamp: 2026-07-24T11:58:24+08:00
- runs: 10
- redis port: 6387
- bench clients: 16
- bench requests: 20000
- stress workers: 2

本目录包含 compare_summary_avg.csv、report.md、run-*/<label>_summary.csv、run-*/<label>_cpu_usage.env、run-*/<label>_throttle.env、run-*/<label>_controlled_pids.txt、run-*/<label>_controlled_pid_cgroups.txt 与 run-*/<label>_background_cgroup_procs.txt。若任一有效性检查失败，会生成 run-*/<label>_invalid_reason.txt 并使脚本退出。
