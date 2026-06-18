# Redis 抗干扰实验报告

## 报告来源

- 结果目录：`/root/EulerPilot/results/reports/redis-20260604-170551`
- 对比汇总文件：`/root/EulerPilot/results/reports/redis-20260604-170551/compare_summary_avg.csv`

## 结果概览

本报告基于多轮 `baseline/default_noisy/active_noisy` 对比结果自动生成。

## 汇总表

| 测试项 | default noisy RPS均值 | active noisy RPS均值 | RPS变化 | default noisy P99均值(ms) | active noisy P99均值(ms) | P99变化 | RPS标准差(active) | P99标准差(active) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| GET | 42105.270 | 23529.410 | -44.12% | 0.367 | 0.367 | 0.00% | 0.000 | 0.000 |
| INCR | 44444.450 | 30769.230 | -30.77% | 0.327 | 0.367 | 12.23% | 0.000 | 0.000 |
| PING_INLINE | 28571.430 | 28571.430 | 0.00% | 0.423 | 0.319 | -24.59% | 0.000 | 0.000 |
| SET | 50000.000 | 34782.610 | -30.43% | 0.319 | 0.303 | -5.02% | 0.000 | 0.000 |

## 自动结论

- `PING_INLINE`：相对 `default_noisy`，RPS 下降 0.00%，P99 改善 -24.59%。
- `SET`：相对 `default_noisy`，RPS 下降 -30.43%，P99 改善 -5.02%。

## Agent 证据摘录

- `run-1`: [Analyzer] redis-server pid=49767 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00366106 latency_exists=yes background_exists=yes cpu_psi_high=no latency_wait_high=no background_runtime_high=no profile=latency_profile trigger_reason=partial-pressure-evidence executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=49813 class=BACKGROUND_NOISY latency_score=0 batch_score=0.6 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=no latency_wait_high=no background_runtime_high=yes profile=latency_profile trigger_reason=partial-pressure-evidence executor=cgroup_v2 group=background action_profile=latency_profile cpu_weight=100 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=49767 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00365227 latency_exists=yes background_exists=yes cpu_psi_high=no latency_wait_high=no background_runtime_high=no profile=latency_profile trigger_reason=partial-pressure-evidence executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=49813 class=BACKGROUND_NOISY latency_score=0 batch_score=0.7 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=no latency_wait_high=no background_runtime_high=yes profile=latency_profile trigger_reason=partial-pressure-evidence executor=cgroup_v2 group=background action_profile=latency_profile cpu_weight=100 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=49767 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.0036526 latency_exists=yes background_exists=yes cpu_psi_high=no latency_wait_high=no background_runtime_high=no profile=latency_profile trigger_reason=partial-pressure-evidence executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=49813 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=no latency_wait_high=no background_runtime_high=yes profile=latency_profile trigger_reason=partial-pressure-evidence executor=cgroup_v2 group=background action_profile=latency_profile cpu_weight=100 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=49767 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00366559 latency_exists=yes background_exists=yes cpu_psi_high=no latency_wait_high=yes background_runtime_high=no profile=latency_profile trigger_reason=partial-pressure-evidence executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=49813 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=no latency_wait_high=no background_runtime_high=yes profile=latency_profile trigger_reason=partial-pressure-evidence executor=cgroup_v2 group=background action_profile=latency_profile cpu_weight=100 cpuset=4-7 applied=yes reason=assigned

## CPU PSI 摘要

- `pre`: some avg10=0.00 avg60=0.00 avg300=0.00 total=70666346
- `pre`: full avg10=0.00 avg60=0.00 avg300=0.00 total=0
- 当前阶段 PSI 阈值仍采用静态阈值方案，后续计划演进为 baseline 自适应阈值。

## 后续建议

- 继续增加重复轮数，降低实验波动。
- 继续优化 Redis 与 stress-ng 的控制策略和目标识别规则。
- 补充更多执行证据和图表化材料。
