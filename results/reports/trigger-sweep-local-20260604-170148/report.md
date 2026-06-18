# PSI 与等待阈值扫描汇总

## 说明

- 当前扫描固定 `latency_weight=1000`、`background_weight=5`。
- 当前扫描固定 `latency_weight=1000`、`background_weight=5`。
- 比较三个触发参数：`cpu_psi_threshold`、`latency_wait_threshold_ns`、`background_runtime_threshold_ns`。
- `wins` 表示同时满足“RPS 不下降且 P99 不变差”的测试项数量。

| cpu_psi_threshold | latency_wait_threshold_ns | background_runtime_threshold_ns | wins | 结果目录 |
| ---: | ---: | ---: | ---: | --- |
| 0.03 | 1000000 | 3000000 | 2 | /root/EulerPilot/results/reports/redis-20260604-170148 |
| 0.03 | 1000000 | 5000000 | 1 | /root/EulerPilot/results/reports/redis-20260604-170237 |
| 0.03 | 3000000 | 3000000 | 3 | /root/EulerPilot/results/reports/redis-20260604-170326 |
| 0.03 | 3000000 | 5000000 | 0 | /root/EulerPilot/results/reports/redis-20260604-170414 |
| 0.05 | 1000000 | 3000000 | 4 | /root/EulerPilot/results/reports/redis-20260604-170503 |
| 0.05 | 1000000 | 5000000 | 1 | /root/EulerPilot/results/reports/redis-20260604-170551 |
| 0.05 | 3000000 | 3000000 | 3 | /root/EulerPilot/results/reports/redis-20260604-170640 |
| 0.05 | 3000000 | 5000000 | 2 | /root/EulerPilot/results/reports/redis-20260604-170728 |

## 关键测试项对比

### 阈值组合：cpu_psi=0.03，latency_wait_ns=1000000，background_runtime_ns=3000000

| 测试项 | RPS变化 | P99变化 |
| --- | ---: | ---: |
| GET | 15.00% | -17.71% |
| SET | -4.00% | -20.07% |
| INCR | 0.00% | -41.47% |
| PING_INLINE | -41.86% | 60.84% |

### 阈值组合：cpu_psi=0.03，latency_wait_ns=1000000，background_runtime_ns=5000000

| 测试项 | RPS变化 | P99变化 |
| --- | ---: | ---: |
| GET | 12.50% | -5.42% |
| SET | -4.17% | -15.72% |
| INCR | -4.17% | -26.37% |
| PING_INLINE | 12.50% | 31.54% |

### 阈值组合：cpu_psi=0.03，latency_wait_ns=3000000，background_runtime_ns=3000000

| 测试项 | RPS变化 | P99变化 |
| --- | ---: | ---: |
| GET | 4.00% | 32.00% |
| SET | 12.50% | -29.48% |
| INCR | 4.00% | -23.98% |
| PING_INLINE | 30.77% | -13.14% |

### 阈值组合：cpu_psi=0.03，latency_wait_ns=3000000，background_runtime_ns=5000000

| 测试项 | RPS变化 | P99变化 |
| --- | ---: | ---: |
| GET | 0.00% | 61.32% |
| SET | 0.00% | 15.84% |
| INCR | -8.33% | 50.20% |
| PING_INLINE | -3.85% | 40.14% |

### 阈值组合：cpu_psi=0.05，latency_wait_ns=1000000，background_runtime_ns=3000000

| 测试项 | RPS变化 | P99变化 |
| --- | ---: | ---: |
| GET | 19.23% | -44.92% |
| SET | 4.55% | -19.10% |
| INCR | 28.00% | -39.54% |
| PING_INLINE | 0.00% | -2.87% |

### 阈值组合：cpu_psi=0.05，latency_wait_ns=1000000，background_runtime_ns=5000000

| 测试项 | RPS变化 | P99变化 |
| --- | ---: | ---: |
| GET | -44.12% | 0.00% |
| SET | -30.43% | -5.02% |
| INCR | -30.77% | 12.23% |
| PING_INLINE | 0.00% | -24.59% |

### 阈值组合：cpu_psi=0.05，latency_wait_ns=3000000，background_runtime_ns=3000000

| 测试项 | RPS变化 | P99变化 |
| --- | ---: | ---: |
| GET | 73.33% | -28.24% |
| SET | 33.33% | -17.20% |
| INCR | 31.58% | -9.13% |
| PING_INLINE | 8.70% | 3.04% |

### 阈值组合：cpu_psi=0.05，latency_wait_ns=3000000，background_runtime_ns=5000000

| 测试项 | RPS变化 | P99变化 |
| --- | ---: | ---: |
| GET | 4.76% | -22.30% |
| SET | 25.00% | -22.79% |
| INCR | -17.39% | -9.13% |
| PING_INLINE | 0.00% | 69.02% |
