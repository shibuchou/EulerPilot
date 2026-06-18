# Redis 抗干扰实验报告

## 报告来源

- 结果目录：`/root/EulerPilot/results/reports/redis-20260603-213133`
- 对比汇总文件：`/root/EulerPilot/results/reports/redis-20260603-213133/compare_summary_avg.csv`

## 结果概览

本报告基于多轮 `baseline/default_noisy/active_noisy` 对比结果自动生成。

## 汇总表

| 测试项 | default noisy RPS均值 | active noisy RPS均值 | RPS变化 | default noisy P99均值(ms) | active noisy P99均值(ms) | P99变化 | RPS标准差(active) | P99标准差(active) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| GET | 34647.550 | 39938.555 | 15.27% | 0.303 | 0.247 | -18.48% | 10861.860 | 0.023 |
| INCR | 36375.665 | 39393.945 | 8.30% | 0.307 | 0.231 | -24.76% | 8570.990 | 0.057 |
| PING_INLINE | 31754.030 | 32795.700 | 3.28% | 0.335 | 0.339 | 1.19% | 760.338 | 0.028 |
| SET | 31527.095 | 37241.380 | 18.12% | 0.339 | 0.267 | -21.24% | 3901.278 | 0.028 |

## 自动结论

- `GET`：相对 `default_noisy`，RPS 提升 15.27%，P99 改善 -18.48%。
- `INCR`：相对 `default_noisy`，RPS 提升 8.30%，P99 改善 -24.76%。
- `PING_INLINE`：相对 `default_noisy`，RPS 提升 3.28%，P99 变差 1.19%。
- `SET`：相对 `default_noisy`，RPS 提升 18.12%，P99 改善 -21.24%。

## Agent 证据摘录

- `run-1`: [Analyzer] stress-ng-cpu pid=31173 class=BACKGROUND_NOISY latency_score=0 batch_score=0 interference_score=0.8 profile=mixed_profile executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=5 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=31128 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00351008 profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=31173 class=BACKGROUND_NOISY latency_score=0 batch_score=0 interference_score=0.8 profile=mixed_profile executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=5 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=31128 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.255093 profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=31128 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00430004 profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=31173 class=BACKGROUND_NOISY latency_score=0 batch_score=0.3 interference_score=0.8 profile=mixed_profile executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=5 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=31173 class=BACKGROUND_NOISY latency_score=0 batch_score=0.5 interference_score=0.8 profile=mixed_profile executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=5 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=31128 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00432892 profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-2`: [Analyzer] redis-server pid=31128 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00365534 profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-2`: [Analyzer] stress-ng-cpu pid=31219 class=BACKGROUND_NOISY latency_score=0 batch_score=0.3 interference_score=0.8 profile=mixed_profile executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=5 cpuset=4-7 applied=yes reason=assigned
- `run-2`: [Analyzer] stress-ng-cpu pid=31219 class=BACKGROUND_NOISY latency_score=0 batch_score=0 interference_score=0.8 profile=mixed_profile executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=5 cpuset=4-7 applied=yes reason=assigned
- `run-2`: [Analyzer] redis-server pid=31128 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00363276 profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-2`: [Analyzer] stress-ng-cpu pid=31219 class=BACKGROUND_NOISY latency_score=0 batch_score=0 interference_score=0.8 profile=mixed_profile executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=5 cpuset=4-7 applied=yes reason=assigned
- `run-2`: [Analyzer] redis-server pid=31128 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.0042358 profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-2`: [Analyzer] stress-ng-cpu pid=31219 class=BACKGROUND_NOISY latency_score=0 batch_score=0 interference_score=0.8 profile=mixed_profile executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=5 cpuset=4-7 applied=yes reason=assigned
- `run-2`: [Analyzer] redis-server pid=31128 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00389682 profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned

## CPU PSI 摘要

- `pre`: some avg10=0.00 avg60=0.00 avg300=0.00 total=11660690
- `pre`: full avg10=0.00 avg60=0.00 avg300=0.00 total=0

## 后续建议

- 继续增加重复轮数，降低实验波动。
- 继续优化 Redis 与 stress-ng 的控制策略和目标识别规则。
- 补充更多执行证据和图表化材料。
