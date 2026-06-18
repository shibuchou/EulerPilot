# background_weight 微调汇总

## 说明

- 当前固定 `latency_weight=1000`。
- 只微调 `background_weight`，用于观察对 `INCR/GET/SET` 的平衡影响。
- `wins` 表示同时满足“RPS 不下降且 P99 不变差”的测试项数量。

| background_weight | cpu_psi_threshold | latency_wait_threshold_ns | background_runtime_threshold_ns | wins | 结果目录 |
| ---: | ---: | ---: | ---: | ---: | --- |
| 5 | 0.05 | 500000 | 2500000 | 4 | /root/EulerPilot/results/reports/redis-20260604-173315 |
| 10 | 0.05 | 500000 | 2500000 | 2 | /root/EulerPilot/results/reports/redis-20260604-173403 |
| 20 | 0.05 | 500000 | 2500000 | 2 | /root/EulerPilot/results/reports/redis-20260604-173452 |

## 关键测试项对比

### 权重组合：background_weight=5，cpu_psi=0.05，latency_wait_ns=500000，background_runtime_ns=2500000

| 测试项 | RPS变化 | P99变化 |
| --- | ---: | ---: |
| GET | 33.33% | -31.54% |
| SET | 8.70% | -31.91% |
| INCR | 41.18% | -41.81% |
| PING_INLINE | 4.17% | -21.49% |

### 权重组合：background_weight=10，cpu_psi=0.05，latency_wait_ns=500000，background_runtime_ns=2500000

| 测试项 | RPS变化 | P99变化 |
| --- | ---: | ---: |
| GET | 23.81% | 55.45% |
| SET | 16.67% | -4.36% |
| INCR | 8.00% | -10.85% |
| PING_INLINE | 0.00% | 12.03% |

### 权重组合：background_weight=20，cpu_psi=0.05，latency_wait_ns=500000，background_runtime_ns=2500000

| 测试项 | RPS变化 | P99变化 |
| --- | ---: | ---: |
| GET | 42.11% | -48.04% |
| SET | 3.85% | 2.23% |
| INCR | 31.58% | 5.90% |
| PING_INLINE | 0.00% | -13.56% |
