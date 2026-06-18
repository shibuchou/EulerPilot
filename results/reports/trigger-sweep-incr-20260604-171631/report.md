# PSI 与等待阈值扫描汇总

## 说明

- 当前扫描固定 `latency_weight=1000`、`background_weight=5`。
- 当前扫描固定 `latency_weight=1000`、`background_weight=5`。
- 比较三个触发参数：`cpu_psi_threshold`、`latency_wait_threshold_ns`、`background_runtime_threshold_ns`。
- `wins` 表示同时满足“RPS 不下降且 P99 不变差”的测试项数量。

| cpu_psi_threshold | latency_wait_threshold_ns | background_runtime_threshold_ns | wins | 结果目录 |
| ---: | ---: | ---: | ---: | --- |
| 0.05 | 500000 | 2500000 | 3 | /root/EulerPilot/results/reports/redis-20260604-171631 |
| 0.05 | 500000 | 3000000 | 1 | /root/EulerPilot/results/reports/redis-20260604-171719 |
| 0.05 | 500000 | 3500000 | 0 | /root/EulerPilot/results/reports/redis-20260604-171808 |
| 0.05 | 1000000 | 2500000 | 2 | /root/EulerPilot/results/reports/redis-20260604-171856 |
| 0.05 | 1000000 | 3000000 | 0 | /root/EulerPilot/results/reports/redis-20260604-171945 |
| 0.05 | 1000000 | 3500000 | 1 | /root/EulerPilot/results/reports/redis-20260604-172033 |
| 0.05 | 1500000 | 2500000 | 2 | /root/EulerPilot/results/reports/redis-20260604-172122 |
| 0.05 | 1500000 | 3000000 | 2 | /root/EulerPilot/results/reports/redis-20260604-172211 |
| 0.05 | 1500000 | 3500000 | 2 | /root/EulerPilot/results/reports/redis-20260604-172259 |

## 关键测试项对比

### 阈值组合：cpu_psi=0.05，latency_wait_ns=500000，background_runtime_ns=2500000

| 测试项 | RPS变化 | P99变化 |
| --- | ---: | ---: |
| GET | 8.00% | -38.40% |
| SET | 18.18% | -26.07% |
| INCR | 17.39% | -38.01% |
| PING_INLINE | -17.24% | 146.01% |

### 阈值组合：cpu_psi=0.05，latency_wait_ns=500000，background_runtime_ns=3000000

| 测试项 | RPS变化 | P99变化 |
| --- | ---: | ---: |
| GET | -21.43% | 25.08% |
| SET | -11.54% | 28.24% |
| INCR | 53.33% | -29.52% |
| PING_INLINE | -3.85% | 39.02% |

### 阈值组合：cpu_psi=0.05，latency_wait_ns=500000，background_runtime_ns=3500000

| 测试项 | RPS变化 | P99变化 |
| --- | ---: | ---: |
| GET | -7.69% | 9.13% |
| SET | -16.67% | 15.84% |
| INCR | 0.00% | 11.15% |
| PING_INLINE | 4.17% | 26.57% |

### 阈值组合：cpu_psi=0.05，latency_wait_ns=1000000，background_runtime_ns=2500000

| 测试项 | RPS变化 | P99变化 |
| --- | ---: | ---: |
| GET | 0.00% | -2.13% |
| SET | 4.17% | -5.90% |
| INCR | 4.35% | 6.08% |
| PING_INLINE | 8.33% | 10.85% |

### 阈值组合：cpu_psi=0.05，latency_wait_ns=1000000，background_runtime_ns=3000000

| 测试项 | RPS变化 | P99变化 |
| --- | ---: | ---: |
| GET | -11.11% | 38.87% |
| SET | -3.85% | -4.09% |
| INCR | -41.18% | 86.58% |
| PING_INLINE | -17.86% | 124.68% |

### 阈值组合：cpu_psi=0.05，latency_wait_ns=1000000，background_runtime_ns=3500000

| 测试项 | RPS变化 | P99变化 |
| --- | ---: | ---: |
| GET | -17.39% | -25.66% |
| SET | -7.14% | -39.31% |
| INCR | 25.00% | -70.93% |
| PING_INLINE | -13.33% | 11.15% |

### 阈值组合：cpu_psi=0.05，latency_wait_ns=1500000，background_runtime_ns=2500000

| 测试项 | RPS变化 | P99变化 |
| --- | ---: | ---: |
| GET | -12.00% | -27.12% |
| SET | 77.78% | -44.34% |
| INCR | 0.00% | -19.51% |
| PING_INLINE | -4.00% | 15.84% |

### 阈值组合：cpu_psi=0.05，latency_wait_ns=1500000，background_runtime_ns=3000000

| 测试项 | RPS变化 | P99变化 |
| --- | ---: | ---: |
| GET | 4.35% | -16.27% |
| SET | 4.76% | -2.71% |
| INCR | 4.55% | 2.64% |
| PING_INLINE | 0.00% | 34.51% |

### 阈值组合：cpu_psi=0.05，latency_wait_ns=1500000，background_runtime_ns=3500000

| 测试项 | RPS变化 | P99变化 |
| --- | ---: | ---: |
| GET | 4.17% | 22.67% |
| SET | 4.17% | -23.15% |
| INCR | 13.04% | -39.07% |
| PING_INLINE | -4.00% | 8.86% |
