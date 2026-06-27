# Nginx Background CPU Quota Sweep Benchmark

- result: `pass`
- RPS retention threshold: `0.85`
- recommended profile: `quota_05`
- recommended cpu.max: `5000 100000`
- recommendation reason: `rps_retention_ge_0.85_and_min_background_ratio`

## Sweep Results

| Profile | cpu.max | RPS | RPS Ratio | p99 Latency | Background Ratio | nr_throttled | throttled_usec |
|---------|---------|-----|-----------|-------------|------------------|--------------|----------------|
| no_quota | `max` | 37784.21 | 1.0000 | 1.67ms | 1.0000 | 0 | 0 |
| quota_50 | `50000 100000` | 38402.91 | 1.0164 | 2.08ms | 0.1259 | 101 | 35285706 |
| quota_20 | `20000 100000` | 39930.23 | 1.0568 | 1.95ms | 0.0498 | 101 | 38320063 |
| quota_10 | `10000 100000` | 37792.34 | 1.0002 | 1.71ms | 0.0249 | 100 | 38980202 |
| quota_05 | `5000 100000` | 37828.85 | 1.0012 | 1.89ms | 0.0125 | 101 | 39881123 |

## Interpretation

This benchmark keeps the Agent placement path consistent and sweeps only the background `cpu.max` pressure profile. The goal is to find a practical resource-control profile that suppresses background CPU pressure while preserving Nginx requests/sec as a boundary signal. It does not claim Nginx performance improvement unless the measured RPS ratios support that conclusion.

## Artifacts

- `sweep_summary.csv`
- `summary.txt`
- `resource_control_events.jsonl`
- `agent.<profile>.log`
- `<profile>.wrk.txt` and `<profile>.summary.csv`
