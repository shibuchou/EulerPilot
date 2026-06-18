# Redis 抗干扰实验报告

## 报告来源

- 结果目录：`/root/EulerPilot/results/reports/redis-20260604-164624`
- 对比汇总文件：`/root/EulerPilot/results/reports/redis-20260604-164624/compare_summary_avg.csv`

## 结果概览

本报告基于多轮 `baseline/default_noisy/active_noisy` 对比结果自动生成。

## 汇总表

| 测试项 | default noisy RPS均值 | active noisy RPS均值 | RPS变化 | default noisy P99均值(ms) | active noisy P99均值(ms) | P99变化 | RPS标准差(active) | P99标准差(active) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| GET | 34782.610 | 36363.640 | 4.55% | 0.335 | 0.215 | -35.82% | 0.000 | 0.000 |
| INCR | 42105.270 | 40000.000 | -5.00% | 0.263 | 0.319 | 21.29% | 0.000 | 0.000 |
| PING_INLINE | 30769.230 | 32000.000 | 4.00% | 0.303 | 0.287 | -5.28% | 0.000 | 0.000 |
| SET | 30769.230 | 32000.000 | 4.00% | 0.295 | 0.247 | -16.27% | 0.000 | 0.000 |

## 自动结论

- `GET`：相对 `default_noisy`，RPS 提升 4.55%，P99 改善 -35.82%。
- `PING_INLINE`：相对 `default_noisy`，RPS 提升 4.00%，P99 改善 -5.28%。
- `SET`：相对 `default_noisy`，RPS 提升 4.00%，P99 改善 -16.27%。

## Agent 证据摘录

- `run-1`: [Analyzer] stress-ng-cpu pid=47292 class=BACKGROUND_NOISY latency_score=0 batch_score=0.7 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=no latency_wait_high=no background_runtime_high=yes profile=latency_profile trigger_reason=partial-pressure-evidence executor=cgroup_v2 group=background action_profile=latency_profile cpu_weight=100 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=47236 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00369519 latency_exists=yes background_exists=yes cpu_psi_high=no latency_wait_high=no background_runtime_high=no profile=latency_profile trigger_reason=partial-pressure-evidence executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=47292 class=BACKGROUND_NOISY latency_score=0 batch_score=0.8 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=no latency_wait_high=no background_runtime_high=yes profile=latency_profile trigger_reason=partial-pressure-evidence executor=cgroup_v2 group=background action_profile=latency_profile cpu_weight=100 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=47236 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.255623 latency_exists=yes background_exists=yes cpu_psi_high=no latency_wait_high=yes background_runtime_high=no profile=latency_profile trigger_reason=partial-pressure-evidence executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned

## CPU PSI 摘要

- `pre`: some avg10=0.00 avg60=0.00 avg300=0.00 total=67866869
- `pre`: full avg10=0.00 avg60=0.00 avg300=0.00 total=0
- 当前阶段 PSI 阈值仍采用静态阈值方案，后续计划演进为 baseline 自适应阈值。

## 后续建议

- 继续增加重复轮数，降低实验波动。
- 继续优化 Redis 与 stress-ng 的控制策略和目标识别规则。
- 补充更多执行证据和图表化材料。
