# Redis 抗干扰实验报告

## 报告来源

- 结果目录：`/root/EulerPilot/results/reports/redis-20260604-161946`
- 对比汇总文件：`/root/EulerPilot/results/reports/redis-20260604-161946/compare_summary_avg.csv`

## 结果概览

本报告基于多轮 `baseline/default_noisy/active_noisy` 对比结果自动生成。

## 汇总表

| 测试项 | default noisy RPS均值 | active noisy RPS均值 | RPS变化 | default noisy P99均值(ms) | active noisy P99均值(ms) | P99变化 | RPS标准差(active) | P99标准差(active) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| GET | 33333.340 | 18867.920 | -43.40% | 0.327 | 0.695 | 112.54% | 0.000 | 0.000 |
| INCR | 33333.340 | 17543.860 | -47.37% | 0.359 | 0.991 | 176.04% | 0.000 | 0.000 |
| PING_INLINE | 29411.760 | 21276.600 | -27.66% | 0.407 | 0.639 | 57.00% | 0.000 | 0.000 |
| SET | 33333.340 | 30303.030 | -9.09% | 0.455 | 0.607 | 33.41% | 0.000 | 0.000 |

## 自动结论

- 当前自动汇总尚未观察到稳定的 active 优势，需要继续调参和重复实验。

## Agent 证据摘录

- `run-1`: [Analyzer] redis-server pid=43095 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=1 cpu_psi_high=yes latency_wait_high=yes background_runtime_high=no profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=44256 class=BACKGROUND_NOISY latency_score=0 batch_score=0.5 interference_score=0.8 cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=mixed_profile executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=5 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=43095 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=1 cpu_psi_high=yes latency_wait_high=yes background_runtime_high=no profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=44256 class=BACKGROUND_NOISY latency_score=0 batch_score=0.8 interference_score=1 cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=mixed_profile executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=5 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=43095 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=1 cpu_psi_high=yes latency_wait_high=yes background_runtime_high=no profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=44256 class=BACKGROUND_NOISY latency_score=0 batch_score=0.9 interference_score=1 cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=mixed_profile executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=5 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=43095 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=1 cpu_psi_high=yes latency_wait_high=yes background_runtime_high=no profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=44256 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=1 cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=mixed_profile executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=5 cpuset=4-7 applied=yes reason=assigned

## CPU PSI 摘要

- `pre`: some avg10=1.06 avg60=1.01 avg300=1.14 total=63830910
- `pre`: full avg10=0.00 avg60=0.00 avg300=0.00 total=0
- 当前阶段 PSI 阈值仍采用静态阈值方案，后续计划演进为 baseline 自适应阈值。

## 后续建议

- 继续增加重复轮数，降低实验波动。
- 继续优化 Redis 与 stress-ng 的控制策略和目标识别规则。
- 补充更多执行证据和图表化材料。
