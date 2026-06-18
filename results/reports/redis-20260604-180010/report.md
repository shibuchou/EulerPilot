# Redis 抗干扰实验报告

## 报告来源

- 结果目录：`/root/EulerPilot/results/reports/redis-20260604-180010`
- 对比汇总文件：`/root/EulerPilot/results/reports/redis-20260604-180010/compare_summary_avg.csv`

## 结果概览

本报告基于多轮 `baseline/default_noisy/active_noisy` 对比结果自动生成。

## 汇总表

| 测试项 | default noisy RPS均值 | active noisy RPS均值 | RPS变化 | default noisy P99均值(ms) | active noisy P99均值(ms) | P99变化 | RPS标准差(active) | P99标准差(active) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| GET | 41923.056 | 36563.010 | -12.79% | 0.517 | 0.505 | -2.32% | 2095.640 | 0.068 |
| INCR | 43798.204 | 39238.238 | -10.41% | 0.516 | 0.489 | -5.23% | 5598.060 | 0.071 |
| PING_INLINE | 41919.610 | 37189.596 | -11.28% | 0.578 | 0.543 | -6.06% | 3130.733 | 0.044 |
| SET | 36995.836 | 36121.126 | -2.36% | 0.511 | 0.559 | 9.39% | 2121.982 | 0.057 |

## 自动结论

- `GET`：相对 `default_noisy`，RPS 下降 -12.79%，P99 改善 -2.32%。
- `INCR`：相对 `default_noisy`，RPS 下降 -10.41%，P99 改善 -5.23%。
- `PING_INLINE`：相对 `default_noisy`，RPS 下降 -11.28%，P99 改善 -6.06%。

## Agent 证据摘录

- `run-1`: [Analyzer] redis-server pid=56333 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00456072 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=yes background_runtime_high=no profile=latency_profile trigger_reason=partial-pressure-evidence executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=56380 class=BACKGROUND_NOISY latency_score=0 batch_score=0 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=no profile=latency_profile trigger_reason=partial-pressure-evidence executor=cgroup_v2 group=background action_profile=latency_profile cpu_weight=100 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=56381 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=latency_profile trigger_reason=partial-pressure-evidence executor=cgroup_v2 group=background action_profile=latency_profile cpu_weight=100 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=56333 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.256565 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=yes background_runtime_high=no profile=latency_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=56380 class=BACKGROUND_NOISY latency_score=0 batch_score=0.5 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=mixed_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=10 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=56381 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=mixed_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=10 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=56333 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.256727 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=yes background_runtime_high=no profile=latency_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=56380 class=BACKGROUND_NOISY latency_score=0 batch_score=0.9 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=mixed_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=10 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=56381 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=mixed_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=10 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=56333 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.256583 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=yes background_runtime_high=no profile=latency_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=56380 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=mixed_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=10 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=56381 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=mixed_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=10 cpuset=4-7 applied=yes reason=assigned
- `run-2`: [Analyzer] redis-server pid=56333 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00375787 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=yes background_runtime_high=no profile=latency_profile trigger_reason=partial-pressure-evidence executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-2`: [Analyzer] stress-ng-cpu pid=56447 class=BACKGROUND_NOISY latency_score=0 batch_score=0.5 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=latency_profile trigger_reason=partial-pressure-evidence executor=cgroup_v2 group=background action_profile=latency_profile cpu_weight=100 cpuset=4-7 applied=yes reason=assigned
- `run-2`: [Analyzer] stress-ng-cpu pid=56446 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=latency_profile trigger_reason=partial-pressure-evidence executor=cgroup_v2 group=background action_profile=latency_profile cpu_weight=100 cpuset=4-7 applied=yes reason=assigned
- `run-2`: [Analyzer] redis-server pid=56333 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00383216 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=yes background_runtime_high=no profile=latency_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-2`: [Analyzer] stress-ng-cpu pid=56447 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=mixed_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=10 cpuset=4-7 applied=yes reason=assigned
- `run-2`: [Analyzer] stress-ng-cpu pid=56446 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=mixed_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=10 cpuset=4-7 applied=yes reason=assigned
- `run-2`: [Analyzer] redis-server pid=56333 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.0038009 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=yes background_runtime_high=no profile=latency_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-2`: [Analyzer] stress-ng-cpu pid=56447 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=mixed_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=10 cpuset=4-7 applied=yes reason=assigned
- `run-2`: [Analyzer] stress-ng-cpu pid=56446 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=mixed_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=10 cpuset=4-7 applied=yes reason=assigned
- `run-2`: [Analyzer] redis-server pid=56333 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00386575 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=yes background_runtime_high=no profile=latency_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-2`: [Analyzer] stress-ng-cpu pid=56447 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=mixed_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=10 cpuset=4-7 applied=yes reason=assigned
- `run-2`: [Analyzer] stress-ng-cpu pid=56446 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=mixed_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=10 cpuset=4-7 applied=yes reason=assigned

## CPU PSI 摘要

- `pre`: some avg10=0.00 avg60=0.03 avg300=0.32 total=84769754
- `pre`: full avg10=0.00 avg60=0.00 avg300=0.00 total=0
- 当前阶段 PSI 阈值仍采用静态阈值方案，后续计划演进为 baseline 自适应阈值。

## 后续建议

- 继续增加重复轮数，降低实验波动。
- 继续优化 Redis 与 stress-ng 的控制策略和目标识别规则。
- 补充更多执行证据和图表化材料。
