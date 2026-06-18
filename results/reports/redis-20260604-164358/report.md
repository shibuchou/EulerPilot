# Redis 抗干扰实验报告

## 报告来源

- 结果目录：`/root/EulerPilot/results/reports/redis-20260604-164358`
- 对比汇总文件：`/root/EulerPilot/results/reports/redis-20260604-164358/compare_summary_avg.csv`

## 结果概览

本报告基于多轮 `baseline/default_noisy/active_noisy` 对比结果自动生成。

## 汇总表

| 测试项 | default noisy RPS均值 | active noisy RPS均值 | RPS变化 | default noisy P99均值(ms) | active noisy P99均值(ms) | P99变化 | RPS标准差(active) | P99标准差(active) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| GET | 36363.640 | 50000.000 | 37.50% | 0.295 | 0.231 | -21.69% | 0.000 | 0.000 |
| INCR | 32000.000 | 53333.340 | 66.67% | 0.367 | 0.175 | -52.32% | 0.000 | 0.000 |
| PING_INLINE | 30769.230 | 34782.610 | 13.04% | 0.391 | 0.303 | -22.51% | 0.000 | 0.000 |
| SET | 44444.450 | 36363.640 | -18.18% | 0.223 | 0.327 | 46.64% | 0.000 | 0.000 |

## 自动结论

- `GET`：相对 `default_noisy`，RPS 提升 37.50%，P99 改善 -21.69%。
- `INCR`：相对 `default_noisy`，RPS 提升 66.67%，P99 改善 -52.32%。
- `PING_INLINE`：相对 `default_noisy`，RPS 提升 13.04%，P99 改善 -22.51%。

## Agent 证据摘录

- `run-1`: [Analyzer] stress-ng-cpu pid=46619 class=BACKGROUND_NOISY latency_score=0 batch_score=0.5 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=no latency_wait_high=no background_runtime_high=yes profile=latency_profile trigger_reason=partial-pressure-evidence executor=cgroup_v2 group=background action_profile=latency_profile cpu_weight=100 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=46575 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00383287 latency_exists=yes background_exists=yes cpu_psi_high=no latency_wait_high=no background_runtime_high=no profile=latency_profile trigger_reason=partial-pressure-evidence executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=46619 class=BACKGROUND_NOISY latency_score=0 batch_score=0.7 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=no latency_wait_high=no background_runtime_high=yes profile=latency_profile trigger_reason=partial-pressure-evidence executor=cgroup_v2 group=background action_profile=latency_profile cpu_weight=100 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=46575 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00377708 latency_exists=yes background_exists=yes cpu_psi_high=no latency_wait_high=no background_runtime_high=no profile=latency_profile trigger_reason=partial-pressure-evidence executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=46619 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=no latency_wait_high=no background_runtime_high=yes profile=latency_profile trigger_reason=partial-pressure-evidence executor=cgroup_v2 group=background action_profile=latency_profile cpu_weight=100 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=46575 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00375938 latency_exists=yes background_exists=yes cpu_psi_high=no latency_wait_high=no background_runtime_high=no profile=latency_profile trigger_reason=partial-pressure-evidence executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=46619 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=no latency_wait_high=no background_runtime_high=yes profile=latency_profile trigger_reason=partial-pressure-evidence executor=cgroup_v2 group=background action_profile=latency_profile cpu_weight=100 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=46575 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00371344 latency_exists=yes background_exists=yes cpu_psi_high=no latency_wait_high=no background_runtime_high=no profile=latency_profile trigger_reason=partial-pressure-evidence executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned

## CPU PSI 摘要

- `pre`: some avg10=0.00 avg60=0.00 avg300=0.00 total=67682585
- `pre`: full avg10=0.00 avg60=0.00 avg300=0.00 total=0
- 当前阶段 PSI 阈值仍采用静态阈值方案，后续计划演进为 baseline 自适应阈值。

## 后续建议

- 继续增加重复轮数，降低实验波动。
- 继续优化 Redis 与 stress-ng 的控制策略和目标识别规则。
- 补充更多执行证据和图表化材料。
