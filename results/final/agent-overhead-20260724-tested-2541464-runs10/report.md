# Agent 自身开销证据

- 结果目录：`/root/eulerpilot-runs/2541464552aa763522a8496a5082a514a843a179/formal-20260723-153923/agent-overhead-runs10`
- 轮数：`10`
- 单轮 Agent 时长：`8.0s`

## 指标口径

- CPU 开销来自 `/proc/<pid>/stat` 的 utime/stime ticks，换算为单核百分比。
- RSS 来自 `/proc/<pid>/status` 的 VmRSS。
- BPF map 以 `bpftool map show pinned ...` 快照保存，只作为内核对象存在性证据。

## 平均结果

| label | present runs | CPU seconds avg | one-core CPU % avg | RSS KB avg | RSS KB max | skipped |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| observe_only_cgroup | 10 | 0.032 | 0.400 | 7855.333 | 16404 | 0 |
| active_cgroup | 10 | 0.039 | 0.487 | 7753.440 | 15504 | 0 |
| active_sched_ext | 10 | 0.043 | 0.537 | 7208.587 | 12164 | 0 |

## 结论边界

本实验用于说明 EulerPilot 用户态控制面本身的 CPU/RSS 级别开销。它不替代 Redis/Nginx 性能对照，也不声称内核调度路径没有成本。
