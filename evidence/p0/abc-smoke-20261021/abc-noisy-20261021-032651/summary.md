# SchedPilot experiment summary

- results: `/root/p0/results/abc-noisy-20261021-032651`
- baseline arm: **A** (openEuler default fair-class scheduler; competition text: default CFS)
- delta is (arm - baseline) / baseline; negative latency delta = better

| arm | n | QPS median [IQR] | QPS delta | goodput@SLO median | goodput delta | p99 med (ms) | p99 delta | p99 delta 95% CI | p99 p-value |
|---|---|---|---|---|---|---|---|---|---|
| A | 3 | 17446.890 [8511.120] | +0.0% | 17312.642 | +0.0% | 4.559 [0.048] | +0.0% | - | - |
| B | 3 | 21166.390 [8662.120] | +21.3% | 20840.901 | +20.4% | 5.111 [0.256] | +12.1% | [+3.0%, +18.7%] | 0.0495 |
| C | 3 | 39658.050 [3451.640] | +127.3% | 39648.409 | +129.0% | 3.287 [0.120] | -27.9% | [-32.0%, -24.5%] | 0.0495 |

## Attribution chain

| step | comparison | QPS delta (median) | p99 delta (median) |
|---|---|---|---|
| sched_ext effect | B vs A | +21.3% | +12.1% |
| classification effect | C vs B | +87.4% | -35.7% |

## Paired per-run comparison vs baseline (same run index)

Mean of per-run paired deltas with 95% CI; `higher`/`lower` are the
number of runs where the arm was above/below the baseline for that metric.

| arm | metric | n | mean Δ% | 95% CI | runs higher | runs lower |
|---|---|---|---|---|---|---|
| B | qps | 3 | +19.71% | [-3.13%, +42.54%] | 3 | 0 |
| B | goodput@SLO | 3 | +18.68% | [-3.15%, +40.51%] | 3 | 0 |
| B | p99 | 3 | +10.84% | [+4.60%, +17.08%] | 3 | 0 |
| C | qps | 3 | +106.10% | [+14.09%, +198.11%] | 3 | 0 |
| C | goodput@SLO | 3 | +107.01% | [+13.57%, +200.45%] | 3 | 0 |
| C | p99 | 3 | -28.26% | [-30.73%, -25.79%] | 0 | 3 |
