# Redis 抗干扰实验报告

## 报告来源

- 结果目录：`/root/EulerPilot/results/reports/redis-20260603-212117`
- 对比汇总文件：`/root/EulerPilot/results/reports/redis-20260603-212117/compare_summary_avg.csv`

## 结果概览

本报告基于多轮 `baseline/default_noisy/active_noisy` 对比结果自动生成。

## 汇总表

| 测试项 | default noisy RPS均值 | active noisy RPS均值 | RPS变化 | default noisy P99均值(ms) | active noisy P99均值(ms) | P99变化 | RPS标准差(active) | P99标准差(active) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| GET | 32258.060 | 32258.060 | 0.00% | 0.311 | 0.415 | 33.44% | 0.000 | 0.000 |
| INCR | 31250.000 | 38461.540 | 23.08% | 0.351 | 0.335 | -4.56% | 0.000 | 0.000 |
| PING_INLINE | 33333.340 | 29411.760 | -11.76% | 0.351 | 0.463 | 31.91% | 0.000 | 0.000 |
| SET | 29411.760 | 52631.580 | 78.95% | 0.375 | 0.207 | -44.80% | 0.000 | 0.000 |

## 自动结论

- `INCR`：相对 `default_noisy`，RPS 提升 23.08%，P99 改善 -4.56%。
- `SET`：相对 `default_noisy`，RPS 提升 78.95%，P99 改善 -44.80%。

## Agent 证据摘录

- `run-1`: [Analyzer] redis-server pid=30179 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00424778 profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=800 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=30293 class=BACKGROUND_NOISY latency_score=0 batch_score=0 interference_score=0.8 profile=mixed_profile executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=5 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=30293 class=BACKGROUND_NOISY latency_score=0 batch_score=0 interference_score=0.8 profile=mixed_profile executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=5 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=30179 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.255358 profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=800 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=30179 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00412602 profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=800 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=30293 class=BACKGROUND_NOISY latency_score=0 batch_score=0.3 interference_score=0.8 profile=mixed_profile executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=5 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=30179 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.0058584 profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=800 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=30293 class=BACKGROUND_NOISY latency_score=0 batch_score=0 interference_score=0.8 profile=mixed_profile executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=5 cpuset=4-7 applied=yes reason=assigned

## CPU PSI 摘要

- `pre`: some avg10=0.00 avg60=0.00 avg300=0.00 total=11219680
- `pre`: full avg10=0.00 avg60=0.00 avg300=0.00 total=0

## 后续建议

- 继续增加重复轮数，降低实验波动。
- 继续优化 Redis 与 stress-ng 的控制策略和目标识别规则。
- 补充更多执行证据和图表化材料。
