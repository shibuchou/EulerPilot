# Mixed Redis + Nginx Background CPU Quota Sweep Benchmark

- result: `pass`
- all-business retention threshold: `0.70`
- recommended profile: `quota_20`
- recommended cpu.max: `20000 100000`
- recommendation reason: `all_business_retention_ge_0.70_and_min_background_ratio`

## Sweep Results

| Profile | cpu.max | Redis GET Ratio | Redis SET Ratio | Nginx RPS Ratio | Business Min Ratio | Nginx p99 | Background Ratio | nr_throttled | throttled_usec |
|---------|---------|-----------------|-----------------|-----------------|--------------------|-----------|------------------|--------------|----------------|
| no_quota | `max` | 1.0000 | 1.0000 | 1.0000 | 1.0000 | 21.79ms | 1.0000 | 0 | 0 |
| quota_50 | `50000 100000` | 0.8378 | 0.8254 | 1.4999 | 0.8254 | 3.38ms | 0.1253 | 101 | 34086954 |
| quota_20 | `20000 100000` | 0.7850 | 0.7073 | 1.6385 | 0.7073 | 1.44ms | 0.0501 | 101 | 37835368 |
| quota_10 | `10000 100000` | 0.6576 | 0.7044 | 1.5722 | 0.6576 | 0.96ms | 0.0251 | 101 | 38894365 |
| quota_05 | `5000 100000` | 0.6220 | 0.6350 | 1.6197 | 0.6220 | 1.12ms | 0.0125 | 108 | 42296555 |

## Interpretation

This benchmark runs Redis and Nginx pressure at the same time while keeping the Agent placement path consistent. It sweeps only the background `cpu.max` pressure profile and uses Redis GET/SET plus Nginx RPS as foreground boundary signals. The pass condition focuses on proving background CPU suppression and throttling, while the recommendation records which profile keeps all foreground ratios above the configured retention threshold when possible.

## Artifacts

- `sweep_summary.csv`
- `summary.txt`
- `resource_control_events.jsonl`
- `agent.<profile>.log`
- `<profile>.redis.csv` and `<profile>.redis.summary.csv`
- `<profile>.wrk.txt` and `<profile>.nginx.summary.csv`
