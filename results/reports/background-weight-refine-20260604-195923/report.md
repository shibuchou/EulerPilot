# background_weight 微调汇总

## 说明

- 当前固定 `latency_weight=1000`。
- 只微调 `background_weight`，用于观察对 `INCR/GET/SET` 的平衡影响。
- `wins` 表示同时满足“RPS 不下降且 P99 不变差”的测试项数量。

| background_weight | cpu_psi_threshold | latency_wait_threshold_ns | background_runtime_threshold_ns | wins | 结果目录 |
| ---: | ---: | ---: | ---: | ---: | --- |
| 10 | 0.05 | 500000 | 2500000 | 0 | /root/EulerPilot/results/reports/redis-20260604-195923 |
| 20 | 0.05 | 500000 | 2500000 | 2 | /root/EulerPilot/results/reports/redis-20260604-200237 |
| 30 | 0.05 | 500000 | 2500000 | 0 | /root/EulerPilot/results/reports/redis-20260604-200550 |
| 50 | 0.05 | 500000 | 2500000 | 0 | /root/EulerPilot/results/reports/redis-20260604-200902 |

## 关键测试项对比

### 权重组合：background_weight=10，cpu_psi=0.05，latency_wait_ns=500000，background_runtime_ns=2500000

| 测试项 | RPS变化 | P99变化 |
| --- | ---: | ---: |
| GET | -21.73% | 15.27% |
| SET | -11.13% | -13.83% |
| INCR | -17.30% | -5.14% |
| PING_INLINE | -13.06% | -8.63% |

### 权重组合：background_weight=20，cpu_psi=0.05，latency_wait_ns=500000，background_runtime_ns=2500000

| 测试项 | RPS变化 | P99变化 |
| --- | ---: | ---: |
| GET | 6.87% | -18.00% |
| SET | -5.26% | 5.98% |
| INCR | -17.26% | -6.14% |
| PING_INLINE | 2.12% | -2.98% |

### 权重组合：background_weight=30，cpu_psi=0.05，latency_wait_ns=500000，background_runtime_ns=2500000

| 测试项 | RPS变化 | P99变化 |
| --- | ---: | ---: |
| GET | -19.33% | 8.26% |
| SET | -0.97% | 7.72% |
| INCR | -28.46% | -1.57% |
| PING_INLINE | -0.60% | -21.27% |

### 权重组合：background_weight=50，cpu_psi=0.05，latency_wait_ns=500000，background_runtime_ns=2500000

| 测试项 | RPS变化 | P99变化 |
| --- | ---: | ---: |
| GET | -2.67% | -18.87% |
| SET | -1.34% | -5.59% |
| INCR | -20.19% | 9.45% |
| PING_INLINE | -3.87% | -17.63% |
