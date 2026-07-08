# Redis 人工静态 vs Agent 动态对比

- 结果目录：`/root/EulerPilot/results/final/redis-static-vs-agent-20260708-162543`
- 轮数：`5`
- stress workers：`2`

## 组别

- `default_noisy`：Redis + stress-ng，无控制。
- `manual_static`：手动把后台 cgroup 写入 `cpu.max=10000 100000`，不启动 Agent。
- `agent_dynamic`：启动 EulerPilot cgroup_v2 active，由 Agent 自动观测、决策和回滚。

## GET 视角核心表

| label | rps_avg | p99_ms_avg | cpu_per_10k_requests_avg | nr_throttled_delta_avg | throttled_usec_delta_avg |
| --- | ---: | ---: | ---: | ---: | ---: |
| default_noisy | 17365.876 | 3.101 | 667.050 | 0.000 | 0.000 |
| manual_static | 17099.272 | 3.090 | 667.750 | 0.000 | 0.000 |
| agent_dynamic | 33108.208 | 1.167 | 563.975 | 8.200 | 651433.400 |

## 结论边界

本实验用于证明 Agent 动态策略能接近人工静态调参效果，并提供自动观测、审计和 rollback。结果不用于声明 Agent 永远超过人工最优参数。
