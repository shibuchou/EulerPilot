# Redis 抗干扰实验报告

## 报告来源

- 结果目录：`/root/EulerPilot/results/reports/redis-20260604-174158`
- 对比汇总文件：`/root/EulerPilot/results/reports/redis-20260604-174158/compare_summary_avg.csv`

## 结果概览

本报告基于多轮 `baseline/default_noisy/active_noisy` 对比结果自动生成。

## 汇总表

| 测试项 | default noisy RPS均值 | active noisy RPS均值 | RPS变化 | default noisy P99均值(ms) | active noisy P99均值(ms) | P99变化 | RPS标准差(active) | P99标准差(active) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| GET | 42547.658 | 36105.290 | -15.14% | 0.490 | 0.527 | 7.55% | 1982.489 | 0.021 |
| INCR | 42082.560 | 36685.256 | -12.83% | 0.573 | 0.532 | -7.16% | 2136.093 | 0.028 |
| PING_INLINE | 39456.776 | 36792.630 | -6.75% | 0.567 | 0.561 | -1.06% | 1574.257 | 0.060 |
| SET | 40586.490 | 35991.946 | -11.32% | 0.540 | 0.557 | 3.15% | 1647.370 | 0.050 |

## 自动结论

- `INCR`：相对 `default_noisy`，RPS 下降 -12.83%，P99 改善 -7.16%。
- `PING_INLINE`：相对 `default_noisy`，RPS 下降 -6.75%，P99 改善 -1.06%。

## Agent 证据摘录

- `run-1`: [Analyzer] stress-ng-cpu pid=54856 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=latency_profile trigger_reason=partial-pressure-evidence executor=cgroup_v2 group=background action_profile=latency_profile cpu_weight=100 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=54810 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00354177 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=yes background_runtime_high=no profile=latency_profile trigger_reason=partial-pressure-evidence executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=54857 class=BACKGROUND_NOISY latency_score=0 batch_score=0.6 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=latency_profile trigger_reason=partial-pressure-evidence executor=cgroup_v2 group=background action_profile=latency_profile cpu_weight=100 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=54856 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=mixed_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=5 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=54810 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.256325 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=yes background_runtime_high=no profile=latency_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=54857 class=BACKGROUND_NOISY latency_score=0 batch_score=0.7 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=mixed_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=5 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=54856 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=mixed_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=5 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=54810 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.256125 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=yes background_runtime_high=no profile=latency_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=54857 class=BACKGROUND_NOISY latency_score=0 batch_score=0.7 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=mixed_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=5 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=54856 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=mixed_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=5 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=54810 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.255999 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=yes background_runtime_high=no profile=latency_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=54857 class=BACKGROUND_NOISY latency_score=0 batch_score=0.8 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=mixed_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=5 cpuset=4-7 applied=yes reason=assigned
- `run-2`: [Analyzer] redis-server pid=54810 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00403357 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=yes background_runtime_high=no profile=latency_profile trigger_reason=partial-pressure-evidence executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-2`: [Analyzer] stress-ng-cpu pid=54906 class=BACKGROUND_NOISY latency_score=0 batch_score=0.6 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=latency_profile trigger_reason=partial-pressure-evidence executor=cgroup_v2 group=background action_profile=latency_profile cpu_weight=100 cpuset=4-7 applied=yes reason=assigned
- `run-2`: [Analyzer] stress-ng-cpu pid=54907 class=BACKGROUND_NOISY latency_score=0 batch_score=0.5 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=latency_profile trigger_reason=partial-pressure-evidence executor=cgroup_v2 group=background action_profile=latency_profile cpu_weight=100 cpuset=4-7 applied=yes reason=assigned
- `run-2`: [Analyzer] redis-server pid=54810 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00421709 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=yes background_runtime_high=no profile=latency_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-2`: [Analyzer] stress-ng-cpu pid=54906 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=mixed_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=5 cpuset=4-7 applied=yes reason=assigned
- `run-2`: [Analyzer] stress-ng-cpu pid=54907 class=BACKGROUND_NOISY latency_score=0 batch_score=0.6 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=mixed_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=5 cpuset=4-7 applied=yes reason=assigned
- `run-2`: [Analyzer] redis-server pid=54810 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00428392 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=yes background_runtime_high=no profile=latency_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-2`: [Analyzer] stress-ng-cpu pid=54906 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=mixed_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=5 cpuset=4-7 applied=yes reason=assigned
- `run-2`: [Analyzer] stress-ng-cpu pid=54907 class=BACKGROUND_NOISY latency_score=0 batch_score=0.9 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=mixed_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=5 cpuset=4-7 applied=yes reason=assigned
- `run-2`: [Analyzer] redis-server pid=54810 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00420095 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=yes background_runtime_high=no profile=latency_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-2`: [Analyzer] stress-ng-cpu pid=54906 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=mixed_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=5 cpuset=4-7 applied=yes reason=assigned
- `run-2`: [Analyzer] stress-ng-cpu pid=54907 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=mixed_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=5 cpuset=4-7 applied=yes reason=assigned

## CPU PSI 摘要

- `pre`: some avg10=0.00 avg60=0.00 avg300=0.00 total=76521297
- `pre`: full avg10=0.00 avg60=0.00 avg300=0.00 total=0
- 当前阶段 PSI 阈值仍采用静态阈值方案，后续计划演进为 baseline 自适应阈值。

## 后续建议

- 继续增加重复轮数，降低实验波动。
- 继续优化 Redis 与 stress-ng 的控制策略和目标识别规则。
- 补充更多执行证据和图表化材料。
