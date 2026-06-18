# Nginx 抗干扰实验报告

## 报告来源

- 结果目录：`/root/EulerPilot/results/reports/nginx-20260605-103350`

## 汇总表

| 阶段 | Requests/sec | Avg Latency | P99 Latency |
| --- | ---: | ---: | ---: |
| baseline | 25920.58 | 301.05us | 627.00us |
| default_noisy | 26834.16 | 290.04us | 582.00us |
| active_noisy | 27054.89 | 287.87us | 552.00us |

## 自动结论

- 相对 `default_noisy`，`active_noisy` 的吞吐变化为 `0.82%`。
- `default_noisy` 的 P99 为 `582.00us`，`active_noisy` 的 P99 为 `552.00us`。

## Agent 证据摘录

- [Analyzer] stress-ng-cpu pid=66591 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=0.8 latency_exists=yes background_exists=yes cpu_psi_high=no latency_wait_high=no background_runtime_high=yes profile=latency_profile trigger_reason=partial-pressure-evidence executor=cgroup_v2 group=background action_profile=latency_profile cpu_weight=100 cpuset=4-7 applied=yes reason=assigned
- [Analyzer] stress-ng-cpu pid=66591 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=1 latency_exists=yes background_exists=yes cpu_psi_high=no latency_wait_high=no background_runtime_high=yes profile=latency_profile trigger_reason=partial-pressure-evidence executor=cgroup_v2 group=background action_profile=latency_profile cpu_weight=100 cpuset=4-7 applied=yes reason=assigned
- [Analyzer] stress-ng-cpu pid=66591 class=BACKGROUND_NOISY latency_score=0 batch_score=1 interference_score=1 latency_exists=yes background_exists=yes cpu_psi_high=yes latency_wait_high=no background_runtime_high=yes profile=mixed_profile trigger_reason=cpu-psi-and-latency-wait-high executor=cgroup_v2 group=background action_profile=mixed_profile cpu_weight=5 cpuset=4-7 applied=yes reason=assigned