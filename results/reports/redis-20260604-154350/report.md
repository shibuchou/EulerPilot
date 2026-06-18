# Redis 抗干扰实验报告

## 报告来源

- 结果目录：`/root/EulerPilot/results/reports/redis-20260604-154350`
- 对比汇总文件：`/root/EulerPilot/results/reports/redis-20260604-154350/compare_summary_avg.csv`

## 结果概览

本报告基于多轮 `baseline/default_noisy/active_noisy` 对比结果自动生成。

## 汇总表

| 测试项 | default noisy RPS均值 | active noisy RPS均值 | RPS变化 | default noisy P99均值(ms) | active noisy P99均值(ms) | P99变化 | RPS标准差(active) | P99标准差(active) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| GET | 39518.150 | 36411.077 | -7.86% | 0.513 | 0.549 | 7.02% | 2267.389 | 0.049 |
| INCR | 39456.518 | 37147.437 | -5.85% | 0.520 | 0.572 | 10.00% | 2670.344 | 0.046 |
| PING_INLINE | 43549.051 | 37012.891 | -15.01% | 0.476 | 0.518 | 8.82% | 2900.419 | 0.034 |
| SET | 39876.423 | 37273.961 | -6.53% | 0.525 | 0.548 | 4.38% | 2485.885 | 0.050 |

## 自动结论

- 当前自动汇总尚未观察到稳定的 active 优势，需要继续调参和重复实验。

## Agent 证据摘录

- `run-1`: [Analyzer] stress-ng-cpu pid=43195 class=BACKGROUND_NOISY latency_score=0 batch_score=0.5 interference_score=0.8 cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=latency_profile executor=cgroup_v2 group=background action_profile=latency_profile cpu_weight=100 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=43196 class=BACKGROUND_NOISY latency_score=0 batch_score=0.7 interference_score=0.8 cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=latency_profile executor=cgroup_v2 group=background action_profile=latency_profile cpu_weight=100 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=43095 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.0023292 cpu_psi_high=yes latency_wait_high=no background_runtime_high=no profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=43195 class=BACKGROUND_NOISY latency_score=0 batch_score=0.7 interference_score=0.8 cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=latency_profile executor=cgroup_v2 group=background action_profile=latency_profile cpu_weight=100 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=43196 class=BACKGROUND_NOISY latency_score=0 batch_score=0.9 interference_score=0.8 cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=latency_profile executor=cgroup_v2 group=background action_profile=latency_profile cpu_weight=100 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=43095 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.253988 cpu_psi_high=yes latency_wait_high=no background_runtime_high=no profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=43195 class=BACKGROUND_NOISY latency_score=0 batch_score=0.8 interference_score=0.8 cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=latency_profile executor=cgroup_v2 group=background action_profile=latency_profile cpu_weight=100 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=43196 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=0.8 cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=latency_profile executor=cgroup_v2 group=background action_profile=latency_profile cpu_weight=100 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=43095 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.25445 cpu_psi_high=yes latency_wait_high=no background_runtime_high=no profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=43195 class=BACKGROUND_NOISY latency_score=0 batch_score=0.9 interference_score=0.8 cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=latency_profile executor=cgroup_v2 group=background action_profile=latency_profile cpu_weight=100 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] stress-ng-cpu pid=43196 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=0.8 cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=latency_profile executor=cgroup_v2 group=background action_profile=latency_profile cpu_weight=100 cpuset=4-7 applied=yes reason=assigned
- `run-1`: [Analyzer] redis-server pid=43095 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.254572 cpu_psi_high=yes latency_wait_high=no background_runtime_high=no profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-2`: [Analyzer] redis-server pid=43095 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00368695 cpu_psi_high=yes latency_wait_high=no background_runtime_high=no profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-2`: [Analyzer] stress-ng-cpu pid=43284 class=BACKGROUND_NOISY latency_score=0 batch_score=0.3 interference_score=0.8 cpu_psi_high=yes latency_wait_high=no background_runtime_high=no profile=latency_profile executor=cgroup_v2 group=background action_profile=latency_profile cpu_weight=100 cpuset=4-7 applied=yes reason=assigned
- `run-2`: [Analyzer] stress-ng-cpu pid=43285 class=BACKGROUND_NOISY latency_score=0 batch_score=0.8 interference_score=0.8 cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=latency_profile executor=cgroup_v2 group=background action_profile=latency_profile cpu_weight=100 cpuset=4-7 applied=yes reason=assigned
- `run-2`: [Analyzer] redis-server pid=43095 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.0036188 cpu_psi_high=yes latency_wait_high=no background_runtime_high=no profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-2`: [Analyzer] stress-ng-cpu pid=43284 class=BACKGROUND_NOISY latency_score=0 batch_score=0.5 interference_score=0.8 cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=latency_profile executor=cgroup_v2 group=background action_profile=latency_profile cpu_weight=100 cpuset=4-7 applied=yes reason=assigned
- `run-2`: [Analyzer] stress-ng-cpu pid=43285 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=0.8 cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=latency_profile executor=cgroup_v2 group=background action_profile=latency_profile cpu_weight=100 cpuset=4-7 applied=yes reason=assigned
- `run-2`: [Analyzer] redis-server pid=43095 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00365796 cpu_psi_high=yes latency_wait_high=no background_runtime_high=no profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-2`: [Analyzer] stress-ng-cpu pid=43284 class=BACKGROUND_NOISY latency_score=0 batch_score=0.6 interference_score=0.8 cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=latency_profile executor=cgroup_v2 group=background action_profile=latency_profile cpu_weight=100 cpuset=4-7 applied=yes reason=assigned
- `run-2`: [Analyzer] stress-ng-cpu pid=43285 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=0.8 cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=latency_profile executor=cgroup_v2 group=background action_profile=latency_profile cpu_weight=100 cpuset=4-7 applied=yes reason=assigned
- `run-2`: [Analyzer] redis-server pid=43095 class=LATENCY_SENSITIVE latency_score=0.85 batch_score=1 interference_score=0.00364614 cpu_psi_high=yes latency_wait_high=no background_runtime_high=no profile=latency_profile executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- `run-2`: [Analyzer] stress-ng-cpu pid=43284 class=BACKGROUND_NOISY latency_score=0 batch_score=0.6 interference_score=0.8 cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=latency_profile executor=cgroup_v2 group=background action_profile=latency_profile cpu_weight=100 cpuset=4-7 applied=yes reason=assigned
- `run-2`: [Analyzer] stress-ng-cpu pid=43285 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=0.8 cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=latency_profile executor=cgroup_v2 group=background action_profile=latency_profile cpu_weight=100 cpuset=4-7 applied=yes reason=assigned

## CPU PSI 摘要

- `pre`: some avg10=0.00 avg60=0.00 avg300=0.00 total=27443074
- `pre`: full avg10=0.00 avg60=0.00 avg300=0.00 total=0
- 当前阶段 PSI 阈值仍采用静态阈值方案，后续计划演进为 baseline 自适应阈值。

## 后续建议

- 继续增加重复轮数，降低实验波动。
- 继续优化 Redis 与 stress-ng 的控制策略和目标识别规则。
- 补充更多执行证据和图表化材料。
