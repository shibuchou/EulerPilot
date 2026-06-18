# Redis 抗干扰实验报告

## 报告来源

- 结果目录：`/root/EulerPilot/results/reports/redis-20260603-213302`
- 对比汇总文件：`/root/EulerPilot/results/reports/redis-20260603-213302/compare_summary_avg.csv`

## 结果概览

本报告基于多轮 `baseline/default_noisy/active_noisy` 对比结果自动生成。

## 汇总表

| 测试项 | default noisy RPS均值 | active noisy RPS均值 | RPS变化 | default noisy P99均值(ms) | active noisy P99均值(ms) | P99变化 | RPS标准差(active) | P99标准差(active) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| GET | 41911.765 | 34523.815 | -17.63% | 0.323 | 0.231 | -28.48% | 1683.586 | 0.023 |
| INCR | 36458.335 | 39596.275 | 8.61% | 0.311 | 0.315 | 1.29% | 5489.956 | 0.017 |
| PING_INLINE | 33908.050 | 37500.005 | 10.59% | 0.287 | 0.271 | -5.57% | 5892.554 | 0.023 |
| SET | 40625.000 | 35759.900 | -11.98% | 0.315 | 0.271 | -13.97% | 1806.149 | 0.023 |

## 自动结论

- `GET`：相对 `default_noisy`，RPS 下降 -17.63%，P99 改善 -28.48%。
- `INCR`：相对 `default_noisy`，RPS 提升 8.61%，P99 变差 1.29%。
- `PING_INLINE`：相对 `default_noisy`，RPS 提升 10.59%，P99 改善 -5.57%。
- `SET`：相对 `default_noisy`，RPS 下降 -11.98%，P99 改善 -13.97%。

## Agent 证据摘录

- `run-1`: [Analyzer] stress-ng-cpu pid=31437 class=BACKGROUND_NOISY latency_score=0 batch_score=0 interference_score=0.8 profile=mixed_profile executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=20 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=31396 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00384868 profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=800 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=31396 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.0037798 profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=800 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=31396 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00423616 profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=800 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=31437 class=BACKGROUND_NOISY latency_score=0 batch_score=0.3 interference_score=0.8 profile=mixed_profile executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=20 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=31437 class=BACKGROUND_NOISY latency_score=0 batch_score=0 interference_score=0.8 profile=mixed_profile executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=20 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=31396 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.0035137 profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=800 cpuset=0-1 applied=yes reason=assigned
- `run-2`: [Analyzer] stress-ng-cpu pid=31555 class=BACKGROUND_NOISY latency_score=0 batch_score=0 interference_score=0.8 profile=mixed_profile executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=20 cpuset=4-7 applied=yes reason=assigned
- `run-2`: [Analyzer] redis-server pid=31396 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00360034 profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=800 cpuset=0-1 applied=yes reason=assigned
- `run-2`: [Analyzer] stress-ng-cpu pid=31555 class=BACKGROUND_NOISY latency_score=0 batch_score=0 interference_score=0.8 profile=mixed_profile executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=20 cpuset=4-7 applied=yes reason=assigned
- `run-2`: [Analyzer] redis-server pid=31396 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00414906 profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=800 cpuset=0-1 applied=yes reason=assigned
- `run-2`: [Analyzer] redis-server pid=31396 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.255884 profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=800 cpuset=0-1 applied=yes reason=assigned
- `run-2`: [Analyzer] stress-ng-cpu pid=31555 class=BACKGROUND_NOISY latency_score=0 batch_score=0 interference_score=0.8 profile=mixed_profile executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=20 cpuset=4-7 applied=yes reason=assigned
- `run-2`: [Analyzer] redis-server pid=31396 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00372906 profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=800 cpuset=0-1 applied=yes reason=assigned
- `run-2`: [Analyzer] stress-ng-cpu pid=31555 class=BACKGROUND_NOISY latency_score=0 batch_score=0.4 interference_score=0.8 profile=mixed_profile executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=20 cpuset=4-7 applied=yes reason=assigned

## CPU PSI 摘要

- `pre`: some avg10=0.00 avg60=0.00 avg300=0.00 total=11750928
- `pre`: full avg10=0.00 avg60=0.00 avg300=0.00 total=0

## 后续建议

- 继续增加重复轮数，降低实验波动。
- 继续优化 Redis 与 stress-ng 的控制策略和目标识别规则。
- 补充更多执行证据和图表化材料。
