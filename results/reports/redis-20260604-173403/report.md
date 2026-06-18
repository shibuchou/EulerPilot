# Redis 抗干扰实验报告

## 报告来源

- 结果目录：`/root/EulerPilot/results/reports/redis-20260604-173403`
- 对比汇总文件：`/root/EulerPilot/results/reports/redis-20260604-173403/compare_summary_avg.csv`

## 结果概览

本报告基于多轮 `baseline/default_noisy/active_noisy` 对比结果自动生成。

## 汇总表

| 测试项 | default noisy RPS均值 | active noisy RPS均值 | RPS变化 | default noisy P99均值(ms) | active noisy P99均值(ms) | P99变化 | RPS标准差(active) | P99标准差(active) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| GET | 30769.230 | 38095.240 | 23.81% | 0.303 | 0.471 | 55.45% | 0.000 | 0.000 |
| INCR | 29629.630 | 32000.000 | 8.00% | 0.295 | 0.263 | -10.85% | 0.000 | 0.000 |
| PING_INLINE | 29629.630 | 29629.630 | 0.00% | 0.399 | 0.447 | 12.03% | 0.000 | 0.000 |
| SET | 28571.430 | 33333.330 | 16.67% | 0.367 | 0.351 | -4.36% | 0.000 | 0.000 |

## 自动结论

- `GET`：相对 `default_noisy`，RPS 提升 23.81%，P99 变差 55.45%。
- `INCR`：相对 `default_noisy`，RPS 提升 8.00%，P99 改善 -10.85%。
- `SET`：相对 `default_noisy`，RPS 提升 16.67%，P99 改善 -4.36%。

## Agent 证据摘录

- `run-1`: [Analyzer] stress-ng-cpu pid=54141 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=no latency_wait_high=no background_runtime_high=yes profile=latency_profile trigger_reason=partial-pressure-evidence executor=cgroup_v2 group=background action_profile=latency_profile cpu_weight=100 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=54096 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00382723 latency_exists=yes background_exists=yes cpu_psi_high=no latency_wait_high=yes background_runtime_high=no profile=latency_profile trigger_reason=partial-pressure-evidence executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=54141 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=no latency_wait_high=no background_runtime_high=yes profile=mixed_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=10 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=54096 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.255615 latency_exists=yes background_exists=yes cpu_psi_high=no latency_wait_high=yes background_runtime_high=no profile=latency_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=54141 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=no latency_wait_high=no background_runtime_high=yes profile=mixed_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=10 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=54096 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.255937 latency_exists=yes background_exists=yes cpu_psi_high=no latency_wait_high=yes background_runtime_high=no profile=latency_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=54141 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=no latency_wait_high=no background_runtime_high=yes profile=mixed_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=10 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=54096 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.256092 latency_exists=yes background_exists=yes cpu_psi_high=no latency_wait_high=yes background_runtime_high=no profile=latency_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned

## CPU PSI 摘要

- `pre`: some avg10=0.00 avg60=0.00 avg300=0.11 total=76071067
- `pre`: full avg10=0.00 avg60=0.00 avg300=0.00 total=0
- 当前阶段 PSI 阈值仍采用静态阈值方案，后续计划演进为 baseline 自适应阈值。

## 后续建议

- 继续增加重复轮数，降低实验波动。
- 继续优化 Redis 与 stress-ng 的控制策略和目标识别规则。
- 补充更多执行证据和图表化材料。
