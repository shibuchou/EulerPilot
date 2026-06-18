# Nginx 抗干扰实验报告

## 报告来源

- 结果目录：`/root/EulerPilot/results/reports/nginx-20260605-110152`

## 汇总表

| 阶段 | Requests/sec | Avg Latency | P99 Latency |
| --- | ---: | ---: | ---: |
| baseline | 30886.02 | 1.04ms | 1.99ms |
| default_noisy | 36112.45 | 0.89ms | 1.75ms |
| active_noisy | 44209.48 | 732.91us | 1.63ms |

## 自动结论

- 相对 `default_noisy`，`active_noisy` 的吞吐变化为 `22.42%`。
- `default_noisy` 的 P99 为 `1.75ms`，`active_noisy` 的 P99 为 `1.63ms`。

## Agent 证据摘录

- [Analyzer] stress-ng-cpu pid=70101 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=mixed_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=20 cpuset=4-7 applied=yes reason=assigned
- [Analyzer] nginx pid=70056 class=LATENCY_SENSITIVE latency_score=1 batch_score=1 interference_score=1 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=yes background_runtime_high=no profile=latency_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- [Analyzer] stress-ng-cpu pid=70100 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=1 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=mixed_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=20 cpuset=4-7 applied=yes reason=assigned
- [Analyzer] stress-ng-cpu pid=70101 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=mixed_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=20 cpuset=4-7 applied=yes reason=assigned
- [Analyzer] nginx pid=70056 class=LATENCY_SENSITIVE latency_score=1 batch_score=1 interference_score=1 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=yes background_runtime_high=no profile=latency_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- [Analyzer] stress-ng-cpu pid=70100 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=1 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=mixed_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=20 cpuset=4-7 applied=yes reason=assigned
- [Analyzer] stress-ng-cpu pid=70101 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=mixed_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=20 cpuset=4-7 applied=yes reason=assigned
- [Analyzer] nginx pid=70056 class=LATENCY_SENSITIVE latency_score=1 batch_score=1 interference_score=1 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=yes background_runtime_high=no profile=latency_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- [Analyzer] stress-ng-cpu pid=70100 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=1 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=mixed_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=20 cpuset=4-7 applied=yes reason=assigned
- [Analyzer] stress-ng-cpu pid=70101 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=mixed_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=20 cpuset=4-7 applied=yes reason=assigned
- [Analyzer] nginx pid=70056 class=LATENCY_SENSITIVE latency_score=1 batch_score=1 interference_score=1 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=yes background_runtime_high=no profile=latency_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- [Analyzer] stress-ng-cpu pid=70100 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=1 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=mixed_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=20 cpuset=4-7 applied=yes reason=assigned