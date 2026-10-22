# SchedPilot experiment summary

- results: `/root/p0/results/abc-quiet-20261021-032302`
- baseline arm: **A** (openEuler default fair-class scheduler; competition text: default CFS)
- delta is (arm - baseline) / baseline; negative latency delta = better

| arm | n | QPS median [IQR] | QPS delta | goodput@SLO median | goodput delta | p99 med (ms) | p99 delta | p99 delta 95% CI | p99 p-value |
|---|---|---|---|---|---|---|---|---|---|
| A | 3 | 47141.210 [4556.290] | +0.0% | 47129.732 | +0.0% | 1.607 [0.072] | +0.0% | - | - |
| B | 3 | 61634.860 [8072.450] | +30.7% | 61634.860 | +30.8% | 1.079 [0.184] | -32.9% | [-50.9%, -18.9%] | 0.0495 |
| C | 3 | 61461.620 [4090.740] | +30.4% | 61461.511 | +30.4% | 1.143 [0.096] | -28.9% | [-37.2%, -17.7%] | 0.0495 |

## Attribution chain

| step | comparison | QPS delta (median) | p99 delta (median) |
|---|---|---|---|
| sched_ext effect | B vs A | +30.7% | -32.9% |
| classification effect | C vs B | -0.3% | +5.9% |

## Paired per-run comparison vs baseline (same run index)

Mean of per-run paired deltas with 95% CI; `higher`/`lower` are the
number of runs where the arm was above/below the baseline for that metric.

| arm | metric | n | mean Δ% | 95% CI | runs higher | runs lower |
|---|---|---|---|---|---|---|
| B | qps | 3 | +33.23% | [+15.54%, +50.92%] | 3 | 0 |
| B | goodput@SLO | 3 | +33.24% | [+15.59%, +50.90%] | 3 | 0 |
| B | p99 | 3 | -34.97% | [-46.59%, -23.35%] | 0 | 3 |
| C | qps | 3 | +31.59% | [+17.42%, +45.77%] | 3 | 0 |
| C | goodput@SLO | 3 | +31.61% | [+17.42%, +45.80%] | 3 | 0 |
| C | p99 | 3 | -27.36% | [-39.34%, -15.37%] | 0 | 3 |
