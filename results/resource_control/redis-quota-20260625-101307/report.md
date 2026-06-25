# Redis + Background CPU Quota Benchmark

- result: pass
- Redis port: `16379`
- clients: `32`
- requests per phase: `30000`
- background workers: `4`
- Resource Control pressure quota: `cpu.max=10000 100000`

## Key Results

| Metric | Default Noisy | EulerPilot Quota |
|--------|---------------|------------------|
| GET RPS | 43795.62 | 37359.90 |
| SET RPS | 44843.05 | 35842.29 |
| Background CPU usec/s | 4039749.82 | 96426.35 |
| nr_throttled delta | 0 | 17 |
| throttled_usec delta | 0 | 6584759 |

## Interpretation

This benchmark compares an unmanaged background CPU noisy workload with the same workload under EulerPilot Resource Control. The pass condition focuses on the control effect: the background cgroup CPU usage rate must drop and cgroup v2 throttling counters must increase under `cpu.max`. Redis throughput is reported as workload evidence, but this result is not claimed as a Redis performance improvement; profile tuning for foreground benefit remains a follow-up benchmark task.

## Artifacts

- `default_noisy.redis.csv`
- `default_noisy.summary.csv`
- `eulerpilot_quota.redis.csv`
- `eulerpilot_quota.summary.csv`
- `resource_control_events.jsonl`
- `agent.log`
- `summary.txt`
