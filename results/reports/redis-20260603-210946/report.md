# Redis 抗干扰实验报告

## 报告来源

- 结果目录：`/root/EulerPilot/results/reports/redis-20260603-210946`
- 对比汇总文件：`/root/EulerPilot/results/reports/redis-20260603-210946/compare_summary_avg.csv`

## 结果概览

本报告基于多轮 `baseline/default_noisy/active_noisy` 对比结果自动生成。

## 汇总表

| 测试项 | default noisy RPS均值 | active noisy RPS均值 | RPS变化 | default noisy P99均值(ms) | active noisy P99均值(ms) | P99变化 | RPS标准差(active) | P99标准差(active) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| GET | 36472.150 | 37087.915 | 1.69% | 0.255 | 0.251 | -1.57% | 1942.599 | 0.006 |
| INCR | 36666.670 | 30555.560 | -16.67% | 0.271 | 0.279 | 2.95% | 3928.374 | 0.023 |
| PING_INLINE | 28571.430 | 23087.685 | -19.19% | 0.331 | 0.567 | 71.30% | 11543.257 | 0.351 |
| SET | 34523.815 | 30952.385 | -10.34% | 0.351 | 0.339 | -3.42% | 3367.179 | 0.040 |

## 自动结论

- `GET`：相对 `default_noisy`，RPS 提升 1.69%，P99 改善 -1.57%。
- `SET`：相对 `default_noisy`，RPS 下降 -10.34%，P99 改善 -3.42%。

## Agent 证据摘录

- `run-1`: [Analyzer] stress-ng-cpu pid=29454 class=BACKGROUND_NOISY latency_score=0 batch_score=0.5 interference_score=0.8 profile=mixed_profile executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=20 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=29409 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00381444 profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=29409 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.255577 profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=29454 class=BACKGROUND_NOISY latency_score=0 batch_score=0 interference_score=0.8 profile=mixed_profile executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=20 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=29409 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.504071 profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=29454 class=BACKGROUND_NOISY latency_score=0 batch_score=0 interference_score=0.8 profile=mixed_profile executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=20 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=29454 class=BACKGROUND_NOISY latency_score=0 batch_score=0 interference_score=0.8 profile=mixed_profile executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=20 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=29409 class=LATENCY_SENSITIVE latency_score=1 batch_score=0.9 interference_score=0.992343 profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-2`: [Analyzer] stress-ng-cpu pid=29582 class=BACKGROUND_NOISY latency_score=0 batch_score=0 interference_score=0.8 profile=mixed_profile executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=20 cpuset=4-7 applied=yes reason=assigned
- `run-2`: [Analyzer] redis-server pid=29409 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00378348 profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-2`: [Analyzer] redis-server pid=29409 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00385308 profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-2`: [Analyzer] redis-server pid=29409 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00435202 profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-2`: [Analyzer] stress-ng-cpu pid=29582 class=BACKGROUND_NOISY latency_score=0 batch_score=0 interference_score=0.8 profile=mixed_profile executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=20 cpuset=4-7 applied=yes reason=assigned
- `run-2`: [Analyzer] stress-ng-cpu pid=29582 class=BACKGROUND_NOISY latency_score=0 batch_score=0 interference_score=0.8 profile=mixed_profile executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=20 cpuset=4-7 applied=yes reason=assigned
- `run-2`: [Analyzer] redis-server pid=29409 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00362334 profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned

## CPU PSI 摘要

- `pre`: some avg10=0.00 avg60=0.00 avg300=0.00 total=10427084
- `pre`: full avg10=0.00 avg60=0.00 avg300=0.00 total=0

## 后续建议

- 继续增加重复轮数，降低实验波动。
- 继续优化 Redis 与 stress-ng 的控制策略和目标识别规则。
- 补充更多执行证据和图表化材料。
