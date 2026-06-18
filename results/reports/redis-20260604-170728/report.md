# Redis 抗干扰实验报告

## 报告来源

- 结果目录：`/root/EulerPilot/results/reports/redis-20260604-170728`
- 对比汇总文件：`/root/EulerPilot/results/reports/redis-20260604-170728/compare_summary_avg.csv`

## 结果概览

本报告基于多轮 `baseline/default_noisy/active_noisy` 对比结果自动生成。

## 汇总表

| 测试项 | default noisy RPS均值 | active noisy RPS均值 | RPS变化 | default noisy P99均值(ms) | active noisy P99均值(ms) | P99变化 | RPS标准差(active) | P99标准差(active) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| GET | 36363.640 | 38095.240 | 4.76% | 0.287 | 0.223 | -22.30% | 0.000 | 0.000 |
| INCR | 42105.270 | 34782.610 | -17.39% | 0.263 | 0.239 | -9.13% | 0.000 | 0.000 |
| PING_INLINE | 29629.630 | 29629.630 | 0.00% | 0.255 | 0.431 | 69.02% | 0.000 | 0.000 |
| SET | 26666.670 | 33333.330 | 25.00% | 0.351 | 0.271 | -22.79% | 0.000 | 0.000 |

## 自动结论

- `GET`：相对 `default_noisy`，RPS 提升 4.76%，P99 改善 -22.30%。
- `INCR`：相对 `default_noisy`，RPS 下降 -17.39%，P99 改善 -9.13%。
- `SET`：相对 `default_noisy`，RPS 提升 25.00%，P99 改善 -22.79%。

## Agent 证据摘录

- `run-1`: [Analyzer] stress-ng-cpu pid=50252 class=BACKGROUND_NOISY latency_score=0 batch_score=0.9 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=no latency_wait_high=no background_runtime_high=yes profile=latency_profile trigger_reason=partial-pressure-evidence executor=cgroup_v2 group=background action_profile=latency_profile cpu_weight=100 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=50206 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00411219 latency_exists=yes background_exists=yes cpu_psi_high=no latency_wait_high=no background_runtime_high=no profile=latency_profile trigger_reason=partial-pressure-evidence executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=50252 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=no latency_wait_high=no background_runtime_high=yes profile=latency_profile trigger_reason=partial-pressure-evidence executor=cgroup_v2 group=background action_profile=latency_profile cpu_weight=100 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=50206 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.256085 latency_exists=yes background_exists=yes cpu_psi_high=no latency_wait_high=no background_runtime_high=no profile=latency_profile trigger_reason=partial-pressure-evidence executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=50252 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=no latency_wait_high=no background_runtime_high=yes profile=latency_profile trigger_reason=partial-pressure-evidence executor=cgroup_v2 group=background action_profile=latency_profile cpu_weight=100 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=50206 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.25593 latency_exists=yes background_exists=yes cpu_psi_high=no latency_wait_high=no background_runtime_high=no profile=latency_profile trigger_reason=partial-pressure-evidence executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=50252 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=no latency_wait_high=no background_runtime_high=yes profile=latency_profile trigger_reason=partial-pressure-evidence executor=cgroup_v2 group=background action_profile=latency_profile cpu_weight=100 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=50206 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.255909 latency_exists=yes background_exists=yes cpu_psi_high=no latency_wait_high=no background_runtime_high=no profile=latency_profile trigger_reason=partial-pressure-evidence executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned

## CPU PSI 摘要

- `pre`: some avg10=0.00 avg60=0.00 avg300=0.00 total=70766779
- `pre`: full avg10=0.00 avg60=0.00 avg300=0.00 total=0
- 当前阶段 PSI 阈值仍采用静态阈值方案，后续计划演进为 baseline 自适应阈值。

## 后续建议

- 继续增加重复轮数，降低实验波动。
- 继续优化 Redis 与 stress-ng 的控制策略和目标识别规则。
- 补充更多执行证据和图表化材料。
