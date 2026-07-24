# Redis 压力递增梯度实验

- 结果目录：`/root/eulerpilot-runs/2541464552aa763522a8496a5082a514a843a179/formal-20260723-153923/redis-pressure-gradient-runs3`
- worker 档位：0 / 1 / 2 / 4 / 8
- 组别：quiet_default / noisy_default / noisy_cgroup_v2 / noisy_scx_psi

## 汇总说明

本实验观察干扰增强时 default、cgroup v2 与 sched_ext psi 的收益和代价变化。结论只限定在 Redis latency-sensitive 混布场景，不推广为所有 workload 的绝对提升。

## GET 视角核心表

| workers | noisy_default_rps_avg | noisy_cgroup_v2_rps_avg | noisy_scx_psi_rps_avg | noisy_default_p99_ms_avg | noisy_cgroup_v2_p99_ms_avg | noisy_scx_psi_p99_ms_avg | noisy_default_cpu_per_10k_requests_avg | noisy_cgroup_v2_cpu_per_10k_requests_avg | noisy_scx_psi_cpu_per_10k_requests_avg |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | 36171.210 | 36342.037 | 8531.840 | 0.402 | 0.423 | 3.452 | 418.083 | 420.750 | 1582.792 |
| 1 | 20556.080 | 26015.433 | 9442.210 | 0.839 | 0.866 | 4.439 | 624.333 | 593.250 | 1578.167 |
| 2 | 16599.850 | 31157.923 | 10404.600 | 3.260 | 2.276 | 5.071 | 660.708 | 588.167 | 1528.708 |
| 4 | 12319.077 | 29105.007 | 9143.727 | 4.850 | 1.871 | 5.492 | 733.125 | 616.583 | 1759.917 |
| 8 | 9292.700 | 33291.173 | 8503.623 | 5.439 | 1.295 | 5.554 | 778.000 | 607.042 | 1923.292 |

## 文件

- `pressure_gradient_summary.csv`：所有 test 的机器可读汇总。
- `workers-*/`：每个压力档位的完整 Redis compare 子结果。
