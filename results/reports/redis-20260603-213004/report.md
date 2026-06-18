# Redis 抗干扰实验报告

## 报告来源

- 结果目录：`/root/EulerPilot/results/reports/redis-20260603-213004`
- 对比汇总文件：`/root/EulerPilot/results/reports/redis-20260603-213004/compare_summary_avg.csv`

## 结果概览

本报告基于多轮 `baseline/default_noisy/active_noisy` 对比结果自动生成。

## 汇总表

| 测试项 | default noisy RPS均值 | active noisy RPS均值 | RPS变化 | default noisy P99均值(ms) | active noisy P99均值(ms) | P99变化 | RPS标准差(active) | P99标准差(active) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| GET | 37087.915 | 36129.030 | -2.59% | 0.323 | 0.271 | -16.10% | 5474.378 | 0.068 |
| INCR | 40151.515 | 38980.510 | -2.92% | 0.263 | 0.255 | -3.04% | 6360.779 | 0.011 |
| PING_INLINE | 30017.920 | 34647.550 | 15.42% | 0.451 | 0.307 | -31.93% | 3379.249 | 0.017 |
| SET | 33333.340 | 36890.645 | 10.67% | 0.279 | 0.251 | -10.04% | 9316.294 | 0.085 |

## 自动结论

- `GET`：相对 `default_noisy`，RPS 下降 -2.59%，P99 改善 -16.10%。
- `INCR`：相对 `default_noisy`，RPS 下降 -2.92%，P99 改善 -3.04%。
- `PING_INLINE`：相对 `default_noisy`，RPS 提升 15.42%，P99 改善 -31.93%。
- `SET`：相对 `default_noisy`，RPS 提升 10.67%，P99 改善 -10.04%。

## Agent 证据摘录

- `run-1`: [Analyzer] stress-ng-cpu pid=30910 class=BACKGROUND_NOISY latency_score=0 batch_score=0 interference_score=0.8 profile=mixed_profile executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=10 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=30867 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00394714 profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=30910 class=BACKGROUND_NOISY latency_score=0 batch_score=0 interference_score=0.8 profile=mixed_profile executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=10 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=30867 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.255736 profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=30910 class=BACKGROUND_NOISY latency_score=0 batch_score=0 interference_score=0.8 profile=mixed_profile executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=10 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=30867 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=0.9 interference_score=0.00389264 profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=30867 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00386668 profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-2`: [Analyzer] stress-ng-cpu pid=30954 class=BACKGROUND_NOISY latency_score=0 batch_score=0.4 interference_score=0.8 profile=mixed_profile executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=10 cpuset=4-7 applied=yes reason=assigned
- `run-2`: [Analyzer] redis-server pid=30867 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00384496 profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-2`: [Analyzer] stress-ng-cpu pid=30954 class=BACKGROUND_NOISY latency_score=0 batch_score=0 interference_score=0.8 profile=mixed_profile executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=10 cpuset=4-7 applied=yes reason=assigned
- `run-2`: [Analyzer] redis-server pid=30867 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00403172 profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-2`: [Analyzer] redis-server pid=30867 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.0036249 profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-2`: [Analyzer] redis-server pid=30867 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00386606 profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned

## CPU PSI 摘要

- `pre`: some avg10=0.00 avg60=0.01 avg300=0.00 total=11572030
- `pre`: full avg10=0.00 avg60=0.00 avg300=0.00 total=0

## 后续建议

- 继续增加重复轮数，降低实验波动。
- 继续优化 Redis 与 stress-ng 的控制策略和目标识别规则。
- 补充更多执行证据和图表化材料。
