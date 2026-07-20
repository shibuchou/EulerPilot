# Agent 自身开销证据

- 结果目录：`/root/EulerPilot/results/final/agent-overhead-20260720-170415`
- 轮数：`3`
- 单轮 Agent 时长：`8.0s`

## 指标口径

- CPU 开销来自 `/proc/<pid>/stat` 的 utime/stime ticks，换算为单核百分比。
- RSS 来自 `/proc/<pid>/status` 的 VmRSS。
- BPF map 以 `bpftool map show pinned ...` 快照保存，只作为内核对象存在性证据。

## 平均结果

| label | present runs | CPU seconds avg | one-core CPU % avg | RSS KB avg | RSS KB max | skipped |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| observe_only_cgroup | 3 | 0.030 | 0.375 | 7933.955 | 15776 | 0 |
| active_cgroup | 3 | 0.030 | 0.375 | 8124.711 | 16208 | 0 |
| active_sched_ext | 3 | 0.040 | 0.500 | 7855.111 | 14584 | 0 |

## 结论边界

本实验用于说明 EulerPilot 用户态控制面本身的 CPU/RSS 级别开销。它不替代 Redis/Nginx 性能对照，也不声称内核调度路径没有成本。
