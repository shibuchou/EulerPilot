# Redis 压力递增梯度实验

- 结果目录：`/root/EulerPilot/results/final/redis-pressure-gradient-20260708-153811`
- worker 档位：0 / 1 / 2 / 4 / 8
- 组别：quiet_default / noisy_default / noisy_cgroup_v2 / noisy_scx_psi

## 汇总说明

本实验观察干扰增强时 default、cgroup v2 与 sched_ext psi 的收益和代价变化。结论只限定在 Redis latency-sensitive 混布场景，不推广为所有 workload 的绝对提升。

## GET 视角核心表

| workers | noisy_default_rps_avg | noisy_cgroup_v2_rps_avg | noisy_scx_psi_rps_avg | noisy_default_p99_ms_avg | noisy_cgroup_v2_p99_ms_avg | noisy_scx_psi_p99_ms_avg | noisy_default_cpu_per_10k_requests_avg | noisy_cgroup_v2_cpu_per_10k_requests_avg | noisy_scx_psi_cpu_per_10k_requests_avg |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | 35843.747 | 35612.683 | 7175.747 | 0.428 | 0.426 | 3.586 | 422.958 | 426.458 | 1532.500 |
| 1 | 20605.703 | 31043.893 | 11165.753 | 1.098 | 0.871 | 4.295 | 620.208 | 573.083 | 1232.167 |
| 2 | 17150.813 | 34357.107 | 10304.917 | 3.234 | 1.516 | 5.167 | 663.958 | 556.542 | 1438.875 |
| 4 | 11797.980 | 32710.083 | 11589.953 | 4.362 | 1.658 | 5.714 | 741.833 | 563.417 | 1418.208 |
| 8 | 7740.483 | 33540.133 | 7348.103 | 7.042 | 1.255 | 9.188 | 793.208 | 572.083 | 1401.042 |

## 文件

- `pressure_gradient_summary.csv`：所有 test 的机器可读汇总。
- `workers-*/`：每个压力档位的完整 Redis compare 子结果。
