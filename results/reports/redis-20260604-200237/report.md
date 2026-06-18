# Redis 抗干扰实验报告

## 报告来源

- 结果目录：`/root/EulerPilot/results/reports/redis-20260604-200237`
- 对比汇总文件：`/root/EulerPilot/results/reports/redis-20260604-200237/compare_summary_avg.csv`

## 结果概览

本报告基于多轮 `baseline/default_noisy/active_noisy` 对比结果自动生成。

## 汇总表

| 测试项 | default noisy RPS均值 | active noisy RPS均值 | RPS变化 | default noisy P99均值(ms) | active noisy P99均值(ms) | P99变化 | RPS标准差(active) | P99标准差(active) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| GET | 35058.467 | 37465.527 | 6.87% | 0.639 | 0.524 | -18.00% | 3283.559 | 0.032 |
| INCR | 42696.057 | 35327.903 | -17.26% | 0.570 | 0.535 | -6.14% | 2029.044 | 0.050 |
| PING_INLINE | 36494.113 | 37266.620 | 2.12% | 0.604 | 0.586 | -2.98% | 2583.424 | 0.082 |
| SET | 39887.917 | 37790.937 | -5.26% | 0.535 | 0.567 | 5.98% | 1326.646 | 0.014 |

## 自动结论

- `GET`：相对 `default_noisy`，RPS 提升 6.87%，P99 改善 -18.00%。
- `INCR`：相对 `default_noisy`，RPS 下降 -17.26%，P99 改善 -6.14%。
- `PING_INLINE`：相对 `default_noisy`，RPS 提升 2.12%，P99 改善 -2.98%。

## Agent 证据摘录

- `run-1`: [Analyzer] redis-server pid=58795 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.25847 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=yes background_runtime_high=no profile=latency_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=58842 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=mixed_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=20 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=58843 class=BACKGROUND_NOISY latency_score=0 batch_score=0.6 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=mixed_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=20 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=58795 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.508075 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=yes background_runtime_high=no profile=latency_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=58842 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=mixed_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=20 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=58843 class=BACKGROUND_NOISY latency_score=0 batch_score=0.8 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=mixed_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=20 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=58795 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.50771 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=yes background_runtime_high=no profile=latency_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=58842 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=mixed_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=20 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=58843 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=mixed_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=20 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=58795 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.757356 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=yes background_runtime_high=no profile=latency_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=58842 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=mixed_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=20 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=58843 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=mixed_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=20 cpuset=4-7 applied=yes reason=assigned
- `run-2`: [Analyzer] redis-server pid=58795 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00423877 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=yes background_runtime_high=no profile=latency_profile trigger_reason=partial-pressure-evidence executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-2`: [Analyzer] stress-ng-cpu pid=58897 class=BACKGROUND_NOISY latency_score=0 batch_score=0.3 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=latency_profile trigger_reason=partial-pressure-evidence executor=cgroup_v2 group=background action_profile=latency_profile cpu_weight=100 cpuset=4-7 applied=yes reason=assigned
- `run-2`: [Analyzer] stress-ng-cpu pid=58898 class=BACKGROUND_NOISY latency_score=0 batch_score=0.3 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=latency_profile trigger_reason=partial-pressure-evidence executor=cgroup_v2 group=background action_profile=latency_profile cpu_weight=100 cpuset=4-7 applied=yes reason=assigned
- `run-2`: [Analyzer] redis-server pid=58795 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00416476 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=yes background_runtime_high=no profile=latency_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-2`: [Analyzer] stress-ng-cpu pid=58897 class=BACKGROUND_NOISY latency_score=0 batch_score=0.7 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=mixed_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=20 cpuset=4-7 applied=yes reason=assigned
- `run-2`: [Analyzer] stress-ng-cpu pid=58898 class=BACKGROUND_NOISY latency_score=0 batch_score=0.3 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=mixed_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=20 cpuset=4-7 applied=yes reason=assigned
- `run-2`: [Analyzer] redis-server pid=58795 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00410808 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=yes background_runtime_high=no profile=latency_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-2`: [Analyzer] stress-ng-cpu pid=58897 class=BACKGROUND_NOISY latency_score=0 batch_score=0.7 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=mixed_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=20 cpuset=4-7 applied=yes reason=assigned
- `run-2`: [Analyzer] stress-ng-cpu pid=58898 class=BACKGROUND_NOISY latency_score=0 batch_score=0.4 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=mixed_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=20 cpuset=4-7 applied=yes reason=assigned
- `run-2`: [Analyzer] redis-server pid=58795 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00403159 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=yes background_runtime_high=no profile=latency_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-2`: [Analyzer] stress-ng-cpu pid=58897 class=BACKGROUND_NOISY latency_score=0 batch_score=0.8 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=mixed_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=20 cpuset=4-7 applied=yes reason=assigned
- `run-2`: [Analyzer] stress-ng-cpu pid=58898 class=BACKGROUND_NOISY latency_score=0 batch_score=0.7 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=mixed_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=20 cpuset=4-7 applied=yes reason=assigned

## CPU PSI 摘要

- `pre`: some avg10=0.53 avg60=0.57 avg300=0.29 total=95827015
- `pre`: full avg10=0.00 avg60=0.00 avg300=0.00 total=0
- 当前阶段 PSI 阈值仍采用静态阈值方案，后续计划演进为 baseline 自适应阈值。

## 后续建议

- 继续增加重复轮数，降低实验波动。
- 继续优化 Redis 与 stress-ng 的控制策略和目标识别规则。
- 补充更多执行证据和图表化材料。
