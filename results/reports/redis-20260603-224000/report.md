# Redis 抗干扰实验报告

## 报告来源

- 结果目录：`/root/EulerPilot/results/reports/redis-20260603-224000`
- 对比汇总文件：`/root/EulerPilot/results/reports/redis-20260603-224000/compare_summary_avg.csv`

## 结果概览

本报告基于多轮 `baseline/default_noisy/active_noisy` 对比结果自动生成。

## 汇总表

| 测试项 | default noisy RPS均值 | active noisy RPS均值 | RPS变化 | default noisy P99均值(ms) | active noisy P99均值(ms) | P99变化 | RPS标准差(active) | P99标准差(active) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| GET | 31250.000 | 27777.780 | -11.11% | 0.351 | 0.375 | 6.84% | 0.000 | 0.000 |
| INCR | 32258.060 | 28571.430 | -11.43% | 0.303 | 0.367 | 21.12% | 0.000 | 0.000 |
| PING_INLINE | 33333.340 | 32258.060 | -3.23% | 0.279 | 0.279 | 0.00% | 0.000 | 0.000 |
| SET | 32258.060 | 34482.760 | 6.90% | 0.343 | 0.279 | -18.66% | 0.000 | 0.000 |

## 自动结论

- `SET`：相对 `default_noisy`，RPS 提升 6.90%，P99 改善 -18.66%。

## Agent 证据摘录

- `run-1`: [Analyzer] stress-ng-cpu pid=36275 class=BACKGROUND_NOISY latency_score=0 batch_score=0 interference_score=0.8 cpu_psi_high=no latency_wait_high=no background_runtime_high=no profile=mixed_profile executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=5 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=36231 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00433768 cpu_psi_high=no latency_wait_high=no background_runtime_high=yes profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=36231 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.256551 cpu_psi_high=no latency_wait_high=no background_runtime_high=yes profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=36275 class=BACKGROUND_NOISY latency_score=0 batch_score=0 interference_score=0.8 cpu_psi_high=no latency_wait_high=no background_runtime_high=no profile=mixed_profile executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=5 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=36275 class=BACKGROUND_NOISY latency_score=0 batch_score=0 interference_score=0.8 cpu_psi_high=no latency_wait_high=no background_runtime_high=no profile=mixed_profile executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=5 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=36231 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=0.9 interference_score=0.00392358 cpu_psi_high=no latency_wait_high=no background_runtime_high=yes profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=36275 class=BACKGROUND_NOISY latency_score=0 batch_score=0 interference_score=0.8 cpu_psi_high=no latency_wait_high=no background_runtime_high=no profile=mixed_profile executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=5 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=36231 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00479336 cpu_psi_high=no latency_wait_high=no background_runtime_high=yes profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned

## CPU PSI 摘要

- `pre`: some avg10=0.00 avg60=0.00 avg300=0.00 total=14489175
- `pre`: full avg10=0.00 avg60=0.00 avg300=0.00 total=0
- 当前阶段 PSI 阈值仍采用静态阈值方案，后续计划演进为 baseline 自适应阈值。

## 后续建议

- 继续增加重复轮数，降低实验波动。
- 继续优化 Redis 与 stress-ng 的控制策略和目标识别规则。
- 补充更多执行证据和图表化材料。
