# Redis 人工静态 vs Agent 动态对比

- 结果目录：`/root/EulerPilot/results/final/redis-static-vs-agent-20260720-114909`
- 轮数：`5`
- stress workers：`2`

## 组别

- `default_noisy`：Redis + stress-ng，无控制。
- `manual_static`：在后台 cgroup 内启动 stress-ng 及其 worker，手动写入 `cpu.max=10000 100000`，不启动 Agent。
- `agent_dynamic`：在同一后台 cgroup 内启动 stress-ng 及其 worker，启动 EulerPilot cgroup_v2 active，由 Agent 自动观测、决策和回滚。

## GET 视角核心表

| label | rps_avg | p99_ms_avg | cpu_per_10k_requests_avg | nr_throttled_delta_avg | throttled_usec_delta_avg |
| --- | ---: | ---: | ---: | ---: | ---: |
| default_noisy | 16249.558 | 3.484 | 672.075 | 0.000 | 0.000 |
| manual_static | 32178.720 | 0.956 | 433.850 | 204.400 | 37259343.000 |
| agent_dynamic | 31612.150 | 1.676 | 579.250 | 9.000 | 765491.200 |

## 结论边界

本实验用于比较同一批后台干扰线程在人工静态控制与 Agent 动态控制下的表现，并提供自动观测、审计和 rollback 证据。结果不用于声明 Agent 永远超过人工最优参数；旧版只移动 stress-ng 父 PID 的结果已撤下，必须以本脚本重跑后的输出作为有效证据。
