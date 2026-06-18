# Redis 抗干扰实验报告

## 报告来源

- 结果目录：`/root/EulerPilot/results/reports/redis-20260603-210558`
- 对比汇总文件：`/root/EulerPilot/results/reports/redis-20260603-210558/compare_summary_avg.csv`

## 结果概览

本报告基于 `compare_summary_avg.csv` 自动生成，用于比较以下三种阶段：

- `baseline`：无后台干扰
- `default_noisy`：有后台干扰，但不启用 EulerPilot 主动控制
- `active_noisy`：有后台干扰，并启用 EulerPilot 主动控制

## 汇总表

| 测试项 | baseline RPS | default noisy RPS | active noisy RPS | default->active RPS变化 | baseline P99(ms) | default noisy P99(ms) | active noisy P99(ms) | default->active P99变化 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| GET | 29411.760 | 40000.000 | 33333.340 | -16.67% | 0.319 | 0.231 | 0.287 | 24.24% |
| INCR | 21276.600 | 43478.260 | 45454.550 | 4.55% | 0.495 | 0.215 | 0.191 | -11.16% |
| PING_INLINE | 29411.760 | 29411.760 | 33333.340 | 13.33% | 0.343 | 0.463 | 0.295 | -36.29% |
| SET | 30303.030 | 37037.040 | 32258.060 | -12.90% | 0.439 | 0.295 | 0.319 | 8.14% |

## 自动结论

- `INCR`：相对 `default_noisy`，RPS 提升 4.55%，P99 改善 -11.16%。
- `PING_INLINE`：相对 `default_noisy`，RPS 提升 13.33%，P99 改善 -36.29%。

## Agent 证据摘录

- `run-1`: [Analyzer] stress-ng-cpu pid=29018 class=BACKGROUND_NOISY latency_score=0 batch_score=0 interference_score=0.8 profile=mixed_profile executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=20 cpuset=4-7 applied=no reason=dry-run
- `run-1`: [Analyzer] redis-server pid=28974 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00366404 profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=no reason=dry-run
- `run-1`: [Analyzer] stress-ng-cpu pid=29018 class=BACKGROUND_NOISY latency_score=0 batch_score=0.3 interference_score=0.8 profile=mixed_profile executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=20 cpuset=4-7 applied=no reason=dry-run
- `run-1`: [Analyzer] redis-server pid=28974 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00403742 profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=no reason=dry-run
- `run-1`: [Analyzer] redis-server pid=28974 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=0.9 interference_score=0.00519607 profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=28974 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.25539 profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=29018 class=BACKGROUND_NOISY latency_score=0 batch_score=0 interference_score=0.8 profile=mixed_profile executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=20 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=28974 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00368 profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=29018 class=BACKGROUND_NOISY latency_score=0 batch_score=0 interference_score=0.8 profile=mixed_profile executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=20 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=28974 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00393058 profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned

## 后续建议

- 继续增加重复轮数，降低实验波动。
- 继续优化 Redis 与 stress-ng 的控制策略和目标识别规则。
- 在报告中继续补充 CPU PSI 摘要和更多执行证据。
