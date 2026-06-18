# PSI 与等待阈值扫描汇总

## 说明

- 当前扫描固定 `latency_weight=1000`、`background_weight=5`。
- 只比较 `cpu_psi_threshold` 与 `latency_wait_threshold_ns` 两个触发参数。
- `wins` 表示同时满足“RPS 不下降且 P99 不变差”的测试项数量。

| cpu_psi_threshold | latency_wait_threshold_ns | wins | 结果目录 |
| ---: | ---: | ---: | --- |
| 0.01 | 1000000 | 1 | /root/EulerPilot/results/reports/redis-20260603-230104 |
| 0.01 | 5000000 | 2 | /root/EulerPilot/results/reports/redis-20260603-230149 |
| 0.05 | 1000000 | 1 | /root/EulerPilot/results/reports/redis-20260603-230235 |
| 0.05 | 5000000 | 4 | /root/EulerPilot/results/reports/redis-20260603-230320 |

## 关键测试项对比

### 阈值组合：cpu_psi=0.01，latency_wait_ns=1000000

| 测试项 | RPS变化 | P99变化 |
| --- | ---: | ---: |
| GET | 19.05% | -21.69% |
| SET | -12.00% | -5.02% |
| INCR | -3.70% | -24.51% |
| PING_INLINE | 3.85% | 3.14% |

### 阈值组合：cpu_psi=0.01，latency_wait_ns=5000000

| 测试项 | RPS变化 | P99变化 |
| --- | ---: | ---: |
| GET | -40.00% | 81.44% |
| SET | 8.00% | -22.46% |
| INCR | 35.29% | -33.45% |
| PING_INLINE | -4.00% | 0.00% |

### 阈值组合：cpu_psi=0.05，latency_wait_ns=1000000

| 测试项 | RPS变化 | P99变化 |
| --- | ---: | ---: |
| GET | 25.00% | 14.34% |
| SET | -4.00% | 6.54% |
| INCR | 8.00% | -21.20% |
| PING_INLINE | -24.00% | -22.51% |

### 阈值组合：cpu_psi=0.05，latency_wait_ns=5000000

| 测试项 | RPS变化 | P99变化 |
| --- | ---: | ---: |
| GET | 54.17% | -57.03% |
| SET | 42.11% | -34.09% |
| INCR | 13.04% | -44.92% |
| PING_INLINE | 60.87% | -28.07% |
