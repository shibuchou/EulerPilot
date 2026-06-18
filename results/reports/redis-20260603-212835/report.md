# Redis 抗干扰实验报告

## 报告来源

- 结果目录：`/root/EulerPilot/results/reports/redis-20260603-212835`
- 对比汇总文件：`/root/EulerPilot/results/reports/redis-20260603-212835/compare_summary_avg.csv`

## 结果概览

本报告基于多轮 `baseline/default_noisy/active_noisy` 对比结果自动生成。

## 汇总表

| 测试项 | default noisy RPS均值 | active noisy RPS均值 | RPS变化 | default noisy P99均值(ms) | active noisy P99均值(ms) | P99变化 | RPS标准差(active) | P99标准差(active) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| GET | 32866.380 | 34855.770 | 6.05% | 0.323 | 0.315 | -2.48% | 5099.329 | 0.096 |
| INCR | 37241.380 | 36666.670 | -1.54% | 0.319 | 0.307 | -3.76% | 4714.040 | 0.062 |
| PING_INLINE | 33908.050 | 33370.410 | -1.59% | 0.291 | 0.255 | -12.37% | 1573.100 | 0.023 |
| SET | 36666.670 | 35185.190 | -4.04% | 0.347 | 0.323 | -6.92% | 2618.911 | 0.040 |

## 自动结论

- `GET`：相对 `default_noisy`，RPS 提升 6.05%，P99 改善 -2.48%。
- `INCR`：相对 `default_noisy`，RPS 下降 -1.54%，P99 改善 -3.76%。
- `PING_INLINE`：相对 `default_noisy`，RPS 下降 -1.59%，P99 改善 -12.37%。
- `SET`：相对 `default_noisy`，RPS 下降 -4.04%，P99 改善 -6.92%。

## Agent 证据摘录

- `run-1`: [Analyzer] redis-server pid=30596 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00296722 profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=30596 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.255674 profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=30640 class=BACKGROUND_NOISY latency_score=0 batch_score=0 interference_score=0.8 profile=mixed_profile executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=20 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=30596 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00398384 profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=30596 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.0039583 profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-2`: [Analyzer] stress-ng-cpu pid=30686 class=BACKGROUND_NOISY latency_score=0 batch_score=0 interference_score=0.8 profile=mixed_profile executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=20 cpuset=4-7 applied=yes reason=assigned
- `run-2`: [Analyzer] redis-server pid=30596 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.0036574 profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-2`: [Analyzer] redis-server pid=30596 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=0.9 interference_score=0.00408133 profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-2`: [Analyzer] stress-ng-cpu pid=30686 class=BACKGROUND_NOISY latency_score=0 batch_score=0 interference_score=0.8 profile=mixed_profile executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=20 cpuset=4-7 applied=yes reason=assigned
- `run-2`: [Analyzer] redis-server pid=30596 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00379008 profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-2`: [Analyzer] redis-server pid=30596 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00383318 profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-2`: [Analyzer] stress-ng-cpu pid=30686 class=BACKGROUND_NOISY latency_score=0 batch_score=0 interference_score=0.8 profile=mixed_profile executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=20 cpuset=4-7 applied=yes reason=assigned

## CPU PSI 摘要

- `pre`: some avg10=0.00 avg60=0.00 avg300=0.00 total=11476265
- `pre`: full avg10=0.00 avg60=0.00 avg300=0.00 total=0

## 后续建议

- 继续增加重复轮数，降低实验波动。
- 继续优化 Redis 与 stress-ng 的控制策略和目标识别规则。
- 补充更多执行证据和图表化材料。
