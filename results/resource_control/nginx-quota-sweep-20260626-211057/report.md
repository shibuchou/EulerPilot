# Nginx Background CPU Quota Sweep Benchmark

- result: `pass`
- RPS retention threshold: `0.85`
- recommended profile: `quota_05`
- recommended cpu.max: `5000 100000`
- recommendation reason: `rps_retention_ge_0.85_and_min_background_ratio`

## Sweep Results

| Profile | cpu.max | RPS | RPS Ratio | p99 Latency | Background Ratio | nr_throttled | throttled_usec |
|---------|---------|-----|-----------|-------------|------------------|--------------|----------------|
| no_quota | `max` | 37837.21 | 1.0000 | 1.90ms | 1.0000 | 0 | 0 |
| quota_50 | `50000 100000` | 44391.54 | 1.1732 | 1.69ms | 0.1250 | 101 | 35284701 |
| quota_20 | `20000 100000` | 36008.27 | 0.9517 | 1.71ms | 0.0499 | 101 | 38348182 |
| quota_10 | `10000 100000` | 65718.33 | 1.7369 | 1.36ms | 0.0249 | 100 | 38953962 |
| quota_05 | `5000 100000` | 44803.89 | 1.1841 | 1.88ms | 0.0125 | 101 | 39857707 |

## Interpretation

This benchmark keeps the Agent placement path consistent and sweeps only the background `cpu.max` pressure profile. The goal is to find a practical resource-control profile that suppresses background CPU pressure while preserving Nginx requests/sec as a boundary signal. It does not claim Nginx performance improvement unless the measured RPS ratios support that conclusion.

## Artifacts

- `sweep_summary.csv`
- `summary.txt`
- `resource_control_events.jsonl`
- `agent.<profile>.log`
- `<profile>.wrk.txt` and `<profile>.summary.csv`
