# PSI 与等待阈值扫描汇总

## 说明

- 当前扫描固定 `latency_weight=1000`、`background_weight=5`。
- 当前扫描固定 `latency_weight=1000`、`background_weight=5`。
- 比较三个触发参数：`cpu_psi_threshold`、`latency_wait_threshold_ns`、`background_runtime_threshold_ns`。
- `wins` 表示同时满足“RPS 不下降且 P99 不变差”的测试项数量。

| cpu_psi_threshold | latency_wait_threshold_ns | background_runtime_threshold_ns | wins | 结果目录 |
| ---: | ---: | ---: | ---: | --- |
| 0.01 | 1000000 | 2000000 | 2 | /root/EulerPilot/results/reports/redis-20260604-164221 |
| 0.01 | 1000000 | 4000000 | 0 | /root/EulerPilot/results/reports/redis-20260604-164310 |
| 0.01 | 5000000 | 2000000 | 3 | /root/EulerPilot/results/reports/redis-20260604-164358 |
| 0.01 | 5000000 | 4000000 | 1 | /root/EulerPilot/results/reports/redis-20260604-164447 |
| 0.05 | 1000000 | 2000000 | 2 | /root/EulerPilot/results/reports/redis-20260604-164535 |
| 0.05 | 1000000 | 4000000 | 3 | /root/EulerPilot/results/reports/redis-20260604-164624 |
| 0.05 | 5000000 | 2000000 | 3 | /root/EulerPilot/results/reports/redis-20260604-164712 |
| 0.05 | 5000000 | 4000000 | 2 | /root/EulerPilot/results/reports/redis-20260604-164801 |

## 关键测试项对比

### 阈值组合：cpu_psi=0.01，latency_wait_ns=1000000，background_runtime_ns=2000000

| 测试项 | RPS变化 | P99变化 |
| --- | ---: | ---: |
| GET | 19.05% | -26.07% |
| SET | 0.00% | -48.70% |
| INCR | -8.33% | -16.37% |
| PING_INLINE | -11.54% | 28.30% |

### 阈值组合：cpu_psi=0.01，latency_wait_ns=1000000，background_runtime_ns=4000000

| 测试项 | RPS变化 | P99变化 |
| --- | ---: | ---: |
| GET | 4.35% | 67.90% |
| SET | -23.08% | -21.62% |
| INCR | 0.00% | 69.69% |
| PING_INLINE | -14.29% | 82.13% |

### 阈值组合：cpu_psi=0.01，latency_wait_ns=5000000，background_runtime_ns=2000000

| 测试项 | RPS变化 | P99变化 |
| --- | ---: | ---: |
| GET | 37.50% | -21.69% |
| SET | -18.18% | 46.64% |
| INCR | 66.67% | -52.32% |
| PING_INLINE | 13.04% | -22.51% |

### 阈值组合：cpu_psi=0.01，latency_wait_ns=5000000，background_runtime_ns=4000000

| 测试项 | RPS变化 | P99变化 |
| --- | ---: | ---: |
| GET | -30.77% | -24.46% |
| SET | -23.81% | -3.24% |
| INCR | 13.64% | -38.87% |
| PING_INLINE | 0.00% | 27.38% |

### 阈值组合：cpu_psi=0.05，latency_wait_ns=1000000，background_runtime_ns=2000000

| 测试项 | RPS变化 | P99变化 |
| --- | ---: | ---: |
| GET | 3.85% | -26.60% |
| SET | 68.42% | -42.46% |
| INCR | -15.38% | -25.07% |
| PING_INLINE | 0.00% | 22.67% |

### 阈值组合：cpu_psi=0.05，latency_wait_ns=1000000，background_runtime_ns=4000000

| 测试项 | RPS变化 | P99变化 |
| --- | ---: | ---: |
| GET | 4.55% | -35.82% |
| SET | 4.00% | -16.27% |
| INCR | -5.00% | 21.29% |
| PING_INLINE | 4.00% | -5.28% |

### 阈值组合：cpu_psi=0.05，latency_wait_ns=5000000，background_runtime_ns=2000000

| 测试项 | RPS变化 | P99变化 |
| --- | ---: | ---: |
| GET | 8.00% | 22.30% |
| SET | 0.00% | -2.64% |
| INCR | 12.50% | -2.71% |
| PING_INLINE | 37.50% | -28.37% |

### 阈值组合：cpu_psi=0.05，latency_wait_ns=5000000，background_runtime_ns=4000000

| 测试项 | RPS变化 | P99变化 |
| --- | ---: | ---: |
| GET | 0.00% | -18.23% |
| SET | -4.35% | -26.91% |
| INCR | -3.85% | 55.75% |
| PING_INLINE | 17.39% | -12.54% |
