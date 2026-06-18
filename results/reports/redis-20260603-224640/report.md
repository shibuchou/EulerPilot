# Redis 抗干扰实验报告

## 报告来源

- 结果目录：`/root/EulerPilot/results/reports/redis-20260603-224640`
- 对比汇总文件：`/root/EulerPilot/results/reports/redis-20260603-224640/compare_summary_avg.csv`

## 结果概览

本报告基于多轮 `baseline/default_noisy/active_noisy` 对比结果自动生成。

## 汇总表

| 测试项 | default noisy RPS均值 | active noisy RPS均值 | RPS变化 | default noisy P99均值(ms) | active noisy P99均值(ms) | P99变化 | RPS标准差(active) | P99标准差(active) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| GET | 38716.124 | 38861.762 | 0.38% | 0.479 | 0.445 | -7.10% | 4107.314 | 0.056 |
| INCR | 46641.734 | 38157.056 | -18.19% | 0.450 | 0.493 | 9.56% | 2233.122 | 0.101 |
| PING_INLINE | 37956.950 | 35617.942 | -6.16% | 0.524 | 0.538 | 2.67% | 2467.476 | 0.061 |
| SET | 39397.548 | 39286.518 | -0.28% | 0.508 | 0.461 | -9.25% | 2790.159 | 0.061 |

## 自动结论

- `GET`：相对 `default_noisy`，RPS 提升 0.38%，P99 改善 -7.10%。
- `SET`：相对 `default_noisy`，RPS 下降 -0.28%，P99 改善 -9.25%。

## Agent 证据摘录

- `run-1`: [Analyzer] stress-ng-cpu pid=36668 class=BACKGROUND_NOISY latency_score=0 batch_score=0.5 interference_score=0.8 cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=latency_profile executor=cgroup_v2 group=background action_profile=latency_profile cpu_weight=100 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=36669 class=BACKGROUND_NOISY latency_score=0 batch_score=0 interference_score=0.8 cpu_psi_high=yes latency_wait_high=no background_runtime_high=no profile=latency_profile executor=cgroup_v2 group=background action_profile=latency_profile cpu_weight=100 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=36623 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.0046418 cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=36668 class=BACKGROUND_NOISY latency_score=0 batch_score=0 interference_score=0.8 cpu_psi_high=yes latency_wait_high=no background_runtime_high=no profile=latency_profile executor=cgroup_v2 group=background action_profile=latency_profile cpu_weight=100 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=36623 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.255607 cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=36623 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.0036314 cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=36668 class=BACKGROUND_NOISY latency_score=0 batch_score=0 interference_score=0.8 cpu_psi_high=yes latency_wait_high=no background_runtime_high=no profile=latency_profile executor=cgroup_v2 group=background action_profile=latency_profile cpu_weight=100 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=36668 class=BACKGROUND_NOISY latency_score=0 batch_score=0 interference_score=0.8 cpu_psi_high=yes latency_wait_high=no background_runtime_high=no profile=latency_profile executor=cgroup_v2 group=background action_profile=latency_profile cpu_weight=100 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=36669 class=BACKGROUND_NOISY latency_score=0 batch_score=0 interference_score=0.8 cpu_psi_high=yes latency_wait_high=no background_runtime_high=no profile=latency_profile executor=cgroup_v2 group=background action_profile=latency_profile cpu_weight=100 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=36623 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00466436 cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-2`: [Analyzer] redis-server pid=36623 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00367816 cpu_psi_high=no latency_wait_high=no background_runtime_high=no profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-2`: [Analyzer] stress-ng-cpu pid=36718 class=BACKGROUND_NOISY latency_score=0 batch_score=0 interference_score=0.8 cpu_psi_high=yes latency_wait_high=no background_runtime_high=no profile=latency_profile executor=cgroup_v2 group=background action_profile=latency_profile cpu_weight=100 cpuset=4-7 applied=yes reason=assigned
- `run-2`: [Analyzer] redis-server pid=36623 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00376578 cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-2`: [Analyzer] redis-server pid=36623 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00389944 cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-2`: [Analyzer] stress-ng-cpu pid=36719 class=BACKGROUND_NOISY latency_score=0 batch_score=0 interference_score=0.8 cpu_psi_high=yes latency_wait_high=no background_runtime_high=no profile=latency_profile executor=cgroup_v2 group=background action_profile=latency_profile cpu_weight=100 cpuset=4-7 applied=yes reason=assigned
- `run-2`: [Analyzer] stress-ng-cpu pid=36718 class=BACKGROUND_NOISY latency_score=0 batch_score=0 interference_score=0.8 cpu_psi_high=yes latency_wait_high=no background_runtime_high=no profile=latency_profile executor=cgroup_v2 group=background action_profile=latency_profile cpu_weight=100 cpuset=4-7 applied=yes reason=assigned
- `run-2`: [Analyzer] redis-server pid=36623 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00418292 cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-2`: [Analyzer] stress-ng-cpu pid=36718 class=BACKGROUND_NOISY latency_score=0 batch_score=0.3 interference_score=0.8 cpu_psi_high=yes latency_wait_high=no background_runtime_high=no profile=latency_profile executor=cgroup_v2 group=background action_profile=latency_profile cpu_weight=100 cpuset=4-7 applied=yes reason=assigned

## CPU PSI 摘要

- `pre`: some avg10=0.00 avg60=0.00 avg300=0.00 total=14691186
- `pre`: full avg10=0.00 avg60=0.00 avg300=0.00 total=0
- 当前阶段 PSI 阈值仍采用静态阈值方案，后续计划演进为 baseline 自适应阈值。

## 后续建议

- 继续增加重复轮数，降低实验波动。
- 继续优化 Redis 与 stress-ng 的控制策略和目标识别规则。
- 补充更多执行证据和图表化材料。
