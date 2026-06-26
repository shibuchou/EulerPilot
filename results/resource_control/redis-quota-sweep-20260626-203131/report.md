# Redis Background CPU Quota Sweep Benchmark

- result: `pass`
- RPS retention threshold: `0.85`
- recommended profile: `quota_05`
- recommended cpu.max: `5000 100000`
- recommendation reason: `rps_retention_ge_0.85_and_min_background_ratio`

## Sweep Results

| Profile | cpu.max | GET RPS | SET RPS | GET Ratio | SET Ratio | Background Ratio | nr_throttled | throttled_usec |
|---------|---------|---------|---------|-----------|-----------|------------------|--------------|----------------|
| no_quota | `max` | 38510.91 | 39840.64 | 1.0000 | 1.0000 | 1.0000 | 0 | 0 |
| quota_50 | `50000 100000` | 44843.05 | 36452.00 | 1.1644 | 0.9149 | 0.1239 | 15 | 5169534 |
| quota_20 | `20000 100000` | 37878.79 | 38167.94 | 0.9836 | 0.9580 | 0.0485 | 16 | 6063364 |
| quota_10 | `10000 100000` | 39840.64 | 42372.88 | 1.0345 | 1.0636 | 0.0247 | 15 | 5836630 |
| quota_05 | `5000 100000` | 36809.82 | 37641.16 | 0.9558 | 0.9448 | 0.0121 | 16 | 6291287 |

## Interpretation

This benchmark keeps the Agent placement path consistent and sweeps only the background `cpu.max` pressure profile. The goal is to find a practical resource-control profile that suppresses background CPU pressure while preserving Redis GET/SET throughput as a boundary signal. It does not claim Redis performance improvement unless the measured RPS ratios support that conclusion.

## Artifacts

- `sweep_summary.csv`
- `summary.txt`
- `resource_control_events.jsonl`
- `agent.<profile>.log`
- `<profile>.redis.csv` and `<profile>.summary.csv`
