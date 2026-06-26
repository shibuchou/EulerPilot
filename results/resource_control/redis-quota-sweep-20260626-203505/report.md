# Redis Background CPU Quota Sweep Benchmark

- result: `pass`
- RPS retention threshold: `0.85`
- recommended profile: `quota_10`
- recommended cpu.max: `10000 100000`
- recommendation reason: `no_profile_met_rps_retention_threshold`

## Sweep Results

| Profile | cpu.max | GET RPS | SET RPS | GET Ratio | SET Ratio | Background Ratio | nr_throttled | throttled_usec |
|---------|---------|---------|---------|-----------|-----------|------------------|--------------|----------------|
| no_quota | `max` | 43541.36 | 43165.47 | 1.0000 | 1.0000 | 1.0000 | 0 | 0 |
| quota_50 | `50000 100000` | 33975.09 | 42735.04 | 0.7803 | 0.9900 | 0.1243 | 16 | 5496845 |
| quota_20 | `20000 100000` | 34013.61 | 34052.21 | 0.7812 | 0.7889 | 0.0494 | 18 | 6829573 |
| quota_10 | `10000 100000` | 36674.82 | 42134.83 | 0.8423 | 0.9761 | 0.0246 | 16 | 6228099 |
| quota_05 | `5000 100000` | 35928.14 | 31914.89 | 0.8251 | 0.7394 | 0.0124 | 18 | 7081094 |

## Interpretation

This benchmark keeps the Agent placement path consistent and sweeps only the background `cpu.max` pressure profile. The goal is to find a practical resource-control profile that suppresses background CPU pressure while preserving Redis GET/SET throughput as a boundary signal. It does not claim Redis performance improvement unless the measured RPS ratios support that conclusion.

## Artifacts

- `sweep_summary.csv`
- `summary.txt`
- `resource_control_events.jsonl`
- `agent.<profile>.log`
- `<profile>.redis.csv` and `<profile>.summary.csv`
