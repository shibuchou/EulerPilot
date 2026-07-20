# Redis 人工静态 vs Agent 动态对比

- 结果目录：`/root/EulerPilot/results/final/redis-static-vs-agent-20260720-150342`
- 轮数：`5`
- stress workers：`2`

## 组别

- `default_noisy`：Redis + stress-ng，干扰线程进入 background cgroup，但不写限额、不启动 Agent。
- `agent_observe_only`：Redis + stress-ng，干扰线程进入 background cgroup，启动 EulerPilot observe-only，不执行控制动作。
- `manual_static`：在后台 cgroup 内启动 stress-ng 及其 worker，手动写入 `cpu.max=10000 100000`，不启动 Agent。
- `agent_dynamic`：在同一后台 cgroup 内启动 stress-ng 及其 worker，启动 EulerPilot cgroup_v2 active，由 Agent 自动观测、决策和回滚。

## 有效性门禁

- 每组均保存 `controlled_pids.txt`、`controlled_pid_cgroups.txt` 与 `background_cgroup_procs.txt`。
- 四组 stress-ng 父进程和 worker 必须位于同一 background cgroup。
- `manual_static` 必须出现 `nr_throttled_delta > 0`，否则该轮生成 `invalid_reason.txt` 并失败退出。
- `cpu_per_10k_requests` 当前来自全系统 `/proc/stat` 同窗口采样，字段 `cpu_metric_scope=system_proc_stat`，只作为辅助指标，不作为目标 cgroup CPU 消耗结论。

## GET 视角核心表

| label | rps_avg | p99_ms_avg | cpu_per_10k_requests_avg | nr_throttled_delta_avg | throttled_usec_delta_avg |
| --- | ---: | ---: | ---: | ---: | ---: |
| default_noisy | 17797.730 | 4.053 | 666.050 | 0.000 | 0.000 |
| agent_observe_only | 16178.344 | 3.404 | 665.325 | 0.000 | 0.000 |
| manual_static | 31547.462 | 1.031 | 432.000 | 205.200 | 37296357.800 |
| agent_dynamic | 31827.504 | 1.650 | 568.875 | 7.400 | 568656.800 |

## 结论边界

本实验用于比较同一类后台干扰线程在无控制、Agent observe-only、人工静态控制与 Agent 动态控制下的表现，并提供自动观测、审计和 rollback 证据。结果不用于声明 Agent 永远超过人工最优参数；旧版只移动 stress-ng 父 PID 的结果已撤下，必须以本脚本重跑后的输出作为有效证据。
