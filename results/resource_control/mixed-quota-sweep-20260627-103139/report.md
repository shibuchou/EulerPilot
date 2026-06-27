# Mixed Redis + Nginx Background CPU Quota Sweep Benchmark

- result: `pass`
- all-business retention threshold: `0.70`
- recommended profile: `quota_50`
- recommended cpu.max: `50000 100000`
- recommendation reason: `all_business_retention_ge_0.70_and_min_background_ratio`

## Sweep Results

| Profile | cpu.max | Redis GET Ratio | Redis SET Ratio | Nginx RPS Ratio | Business Min Ratio | Nginx p99 | Background Ratio | nr_throttled | throttled_usec |
|---------|---------|-----------------|-----------------|-----------------|--------------------|-----------|------------------|--------------|----------------|
| no_quota | `max` | 1.0000 | 1.0000 | 1.0000 | 1.0000 | 4.07ms | 1.0000 | 0 | 0 |
| quota_50 | `50000 100000` | 0.7681 | 0.8832 | 1.0822 | 0.7681 | 1.90ms | 0.1255 | 101 | 34148712 |
| quota_20 | `20000 100000` | 0.6896 | 0.6456 | 1.1988 | 0.6456 | 1.13ms | 0.0502 | 101 | 37761132 |
| quota_10 | `10000 100000` | 0.6258 | 0.6647 | 1.2121 | 0.6258 | 1.34ms | 0.0251 | 103 | 39788733 |
| quota_05 | `5000 100000` | 0.5824 | 0.5823 | 1.2126 | 0.5823 | 1.07ms | 0.0126 | 114 | 44745549 |

## Interpretation

This benchmark runs Redis and Nginx pressure at the same time while keeping the Agent placement path consistent. It sweeps only the background `cpu.max` pressure profile and uses Redis GET/SET plus Nginx RPS as foreground boundary signals. The pass condition focuses on proving background CPU suppression and throttling, while the recommendation records which profile keeps all foreground ratios above the configured retention threshold when possible.

## Artifacts

- `sweep_summary.csv`
- `summary.txt`
- `resource_control_events.jsonl`
- `agent.<profile>.log`
- `<profile>.redis.csv` and `<profile>.redis.summary.csv`
- `<profile>.wrk.txt` and `<profile>.nginx.summary.csv`
