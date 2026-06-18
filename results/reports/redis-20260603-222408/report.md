# Redis 抗干扰实验报告

## 报告来源

- 结果目录：`/root/EulerPilot/results/reports/redis-20260603-222408`
- 对比汇总文件：`/root/EulerPilot/results/reports/redis-20260603-222408/compare_summary_avg.csv`

## 结果概览

本报告基于多轮 `baseline/default_noisy/active_noisy` 对比结果自动生成。

## 汇总表

| 测试项 | default noisy RPS均值 | active noisy RPS均值 | RPS变化 | default noisy P99均值(ms) | active noisy P99均值(ms) | P99变化 | RPS标准差(active) | P99标准差(active) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| GET | 29411.760 | 58823.530 | 100.00% | 0.375 | 0.263 | -29.87% | 0.000 | 0.000 |
| INCR | 33333.340 | 43478.260 | 30.43% | 0.255 | 0.295 | 15.69% | 0.000 | 0.000 |
| PING_INLINE | 33333.340 | 43478.260 | 30.43% | 0.319 | 0.303 | -5.02% | 0.000 | 0.000 |
| SET | 34482.760 | 45454.550 | 31.82% | 0.271 | 0.263 | -2.95% | 0.000 | 0.000 |

## 自动结论

- `GET`：相对 `default_noisy`，RPS 提升 100.00%，P99 改善 -29.87%。
- `INCR`：相对 `default_noisy`，RPS 提升 30.43%，P99 变差 15.69%。
- `PING_INLINE`：相对 `default_noisy`，RPS 提升 30.43%，P99 改善 -5.02%。
- `SET`：相对 `default_noisy`，RPS 提升 31.82%，P99 改善 -2.95%。

## Agent 证据摘录

- `run-1`: [Analyzer] redis-server pid=34471 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00374298 cpu_psi_high=no latency_wait_high=no background_runtime_high=yes profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=34514 class=BACKGROUND_NOISY latency_score=0 batch_score=0 interference_score=0.8 cpu_psi_high=no latency_wait_high=no background_runtime_high=no profile=mixed_profile executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=5 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=34471 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.25603 cpu_psi_high=no latency_wait_high=no background_runtime_high=yes profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=34514 class=BACKGROUND_NOISY latency_score=0 batch_score=0 interference_score=0.8 cpu_psi_high=no latency_wait_high=no background_runtime_high=no profile=mixed_profile executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=5 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=34471 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00374312 cpu_psi_high=no latency_wait_high=no background_runtime_high=yes profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=34514 class=BACKGROUND_NOISY latency_score=0 batch_score=0 interference_score=0.8 cpu_psi_high=no latency_wait_high=no background_runtime_high=no profile=mixed_profile executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=5 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=34514 class=BACKGROUND_NOISY latency_score=0 batch_score=0 interference_score=0.8 cpu_psi_high=no latency_wait_high=no background_runtime_high=no profile=mixed_profile executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=5 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=34471 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00404152 cpu_psi_high=no latency_wait_high=no background_runtime_high=yes profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned

## CPU PSI 摘要

- `pre`: some avg10=0.00 avg60=0.00 avg300=0.00 total=13677312
- `pre`: full avg10=0.00 avg60=0.00 avg300=0.00 total=0
- 当前阶段 PSI 阈值仍采用静态阈值方案，后续计划演进为 baseline 自适应阈值。

## 后续建议

- 继续增加重复轮数，降低实验波动。
- 继续优化 Redis 与 stress-ng 的控制策略和目标识别规则。
- 补充更多执行证据和图表化材料。
