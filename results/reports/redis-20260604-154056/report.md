# Redis 抗干扰实验报告

## 报告来源

- 结果目录：`/root/EulerPilot/results/reports/redis-20260604-154056`
- 对比汇总文件：`/root/EulerPilot/results/reports/redis-20260604-154056/compare_summary_avg.csv`

## 结果概览

本报告基于多轮 `baseline/default_noisy/active_noisy` 对比结果自动生成。

## 汇总表

| 测试项 | default noisy RPS均值 | active noisy RPS均值 | RPS变化 | default noisy P99均值(ms) | active noisy P99均值(ms) | P99变化 | RPS标准差(active) | P99标准差(active) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| GET | 37037.040 | 32258.060 | -12.90% | 0.271 | 0.247 | -8.86% | 0.000 | 0.000 |
| INCR | 33333.340 | 43478.260 | 30.43% | 0.255 | 0.351 | 37.65% | 0.000 | 0.000 |
| PING_INLINE | 31250.000 | 32258.060 | 3.23% | 0.263 | 0.335 | 27.38% | 0.000 | 0.000 |
| SET | 34482.760 | 34482.760 | 0.00% | 0.247 | 0.271 | 9.72% | 0.000 | 0.000 |

## 自动结论

- `GET`：相对 `default_noisy`，RPS 下降 -12.90%，P99 改善 -8.86%。
- `INCR`：相对 `default_noisy`，RPS 提升 30.43%，P99 变差 37.65%。
- `PING_INLINE`：相对 `default_noisy`，RPS 提升 3.23%，P99 变差 27.38%。

## Agent 证据摘录

- `run-1`: [Analyzer] redis-server pid=42767 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.0127893 cpu_psi_high=no latency_wait_high=no background_runtime_high=no profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=42813 class=BACKGROUND_NOISY latency_score=0 batch_score=0.6 interference_score=0.8 cpu_psi_high=no latency_wait_high=no background_runtime_high=yes profile=latency_profile executor=cgroup_v2 group=background action_profile=latency_profile cpu_weight=100 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=42767 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.0105894 cpu_psi_high=no latency_wait_high=no background_runtime_high=no profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=42813 class=BACKGROUND_NOISY latency_score=0 batch_score=0.8 interference_score=0.8 cpu_psi_high=no latency_wait_high=no background_runtime_high=yes profile=latency_profile executor=cgroup_v2 group=background action_profile=latency_profile cpu_weight=100 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=42767 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00928799 cpu_psi_high=no latency_wait_high=no background_runtime_high=no profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=42813 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=0.8 cpu_psi_high=no latency_wait_high=no background_runtime_high=yes profile=latency_profile executor=cgroup_v2 group=background action_profile=latency_profile cpu_weight=100 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=42767 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00837348 cpu_psi_high=no latency_wait_high=no background_runtime_high=no profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=42813 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=0.8 cpu_psi_high=no latency_wait_high=no background_runtime_high=yes profile=latency_profile executor=cgroup_v2 group=background action_profile=latency_profile cpu_weight=100 cpuset=4-7 applied=yes reason=assigned

## CPU PSI 摘要

- `pre`: some avg10=0.00 avg60=0.00 avg300=0.00 total=27276566
- `pre`: full avg10=0.00 avg60=0.00 avg300=0.00 total=0
- 当前阶段 PSI 阈值仍采用静态阈值方案，后续计划演进为 baseline 自适应阈值。

## 后续建议

- 继续增加重复轮数，降低实验波动。
- 继续优化 Redis 与 stress-ng 的控制策略和目标识别规则。
- 补充更多执行证据和图表化材料。
