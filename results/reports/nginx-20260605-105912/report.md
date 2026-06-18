# Nginx 抗干扰实验报告

## 报告来源

- 结果目录：`/root/EulerPilot/results/reports/nginx-20260605-105912`

## 汇总表

| 阶段 | Requests/sec | Avg Latency | P99 Latency |
| --- | ---: | ---: | ---: |
| baseline | 27488.64 | 1.16ms | 2.02ms |
| default_noisy | 33234.93 | 0.97ms | 2.06ms |
| active_noisy | 45070.48 | 786.15us | 4.21ms |

## 自动结论

- 相对 `default_noisy`，`active_noisy` 的吞吐变化为 `35.61%`。
- `default_noisy` 的 P99 为 `2.06ms`，`active_noisy` 的 P99 为 `4.21ms`。

## Agent 证据摘录

- [Analyzer] nginx pid=69219 class=LATENCY_SENSITIVE latency_score=1 batch_score=1 interference_score=1 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=yes background_runtime_high=no profile=latency_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- [Analyzer] stress-ng-cpu pid=69676 class=BACKGROUND_NOISY latency_score=0 batch_score=0.5 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=mixed_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=20 cpuset=4-7 applied=yes reason=assigned
- [Analyzer] stress-ng-cpu pid=69675 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=mixed_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=20 cpuset=4-7 applied=yes reason=assigned
- [Analyzer] nginx pid=69219 class=LATENCY_SENSITIVE latency_score=1 batch_score=1 interference_score=1 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=yes background_runtime_high=no profile=latency_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- [Analyzer] stress-ng-cpu pid=69676 class=BACKGROUND_NOISY latency_score=0 batch_score=0.8 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=mixed_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=20 cpuset=4-7 applied=yes reason=assigned
- [Analyzer] stress-ng-cpu pid=69675 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=mixed_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=20 cpuset=4-7 applied=yes reason=assigned
- [Analyzer] nginx pid=69219 class=LATENCY_SENSITIVE latency_score=1 batch_score=1 interference_score=1 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=yes background_runtime_high=no profile=latency_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- [Analyzer] stress-ng-cpu pid=69676 class=BACKGROUND_NOISY latency_score=0 batch_score=0.9 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=mixed_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=20 cpuset=4-7 applied=yes reason=assigned
- [Analyzer] stress-ng-cpu pid=69675 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=mixed_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=20 cpuset=4-7 applied=yes reason=assigned
- [Analyzer] nginx pid=69219 class=LATENCY_SENSITIVE latency_score=1 batch_score=1 interference_score=1 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=yes background_runtime_high=no profile=latency_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=latency action_profile=latency_profile cpu_weight=1000 cpuset=0-1 applied=yes reason=assigned
- [Analyzer] stress-ng-cpu pid=69676 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=mixed_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=20 cpuset=4-7 applied=yes reason=assigned
- [Analyzer] stress-ng-cpu pid=69675 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=mixed_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=20 cpuset=4-7 applied=yes reason=assigned