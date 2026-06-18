# background_weight 微调汇总

## 说明

- 当前固定 `latency_weight=1000`。
- 只微调 `background_weight`，用于观察对 `INCR/GET/SET` 的平衡影响。
- `wins` 表示同时满足“RPS 不下降且 P99 不变差”的测试项数量。

| background_weight | cpu_psi_threshold | latency_wait_threshold_ns | background_runtime_threshold_ns | wins | 结果目录 |
| ---: | ---: | ---: | ---: | ---: | --- |
| 5 | 0.05 | 500000 | 2500000 | 1 | /root/EulerPilot/results/reports/redis-20260604-175044 |
| 10 | 0.05 | 500000 | 2500000 | 2 | /root/EulerPilot/results/reports/redis-20260604-175357 |

## 关键测试项对比

### 权重组合：background_weight=5，cpu_psi=0.05，latency_wait_ns=500000，background_runtime_ns=2500000

| 测试项 | RPS变化 | P99变化 |
| --- | ---: | ---: |
| GET | -25.59% | 1.05% |
| SET | -7.85% | 5.06% |
| INCR | 1.95% | -9.73% |
| PING_INLINE | -9.26% | -5.72% |

### 权重组合：background_weight=10，cpu_psi=0.05，latency_wait_ns=500000，background_runtime_ns=2500000

| 测试项 | RPS变化 | P99变化 |
| --- | ---: | ---: |
| GET | 14.06% | -25.57% |
| SET | 6.75% | -13.39% |
| INCR | -8.04% | -18.86% |
| PING_INLINE | -3.47% | -23.19% |
