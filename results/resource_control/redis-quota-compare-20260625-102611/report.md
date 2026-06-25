# Redis + Background CPU Quota Compare Benchmark

- result: pass
- Redis port: `16380`
- clients: `32`
- requests per phase: `30000`
- background workers: `4`
- quota: `cpu.max=10000 100000`

## Key Results

| Metric | Default Noisy | EulerPilot No Quota | EulerPilot Quota |
|--------|---------------|---------------------|------------------|
| GET RPS | 42857.14 | 40650.41 | 36719.71 |
| SET RPS | 44247.79 | 41493.78 | 35842.29 |
| Background CPU usec/s | 4041108.91 | 4038245.65 | 100796.10 |
| nr_throttled delta | 0 | 0 | 17 |
| throttled_usec delta | 0 | 0 | 6621902 |

## Interpretation

This benchmark separates Agent placement effects from CPU quota effects. The `eulerpilot_no_quota` phase runs the same Agent loop without a background CPU cap, while `eulerpilot_quota` adds `cpu.max=10000 100000`. The pass condition focuses on whether the background cgroup CPU usage drops and throttling counters increase. Redis throughput is recorded as foreground evidence; it is not claimed as an improvement unless the RPS ratios support that claim.

## Artifacts

- `default_noisy.redis.csv`
- `eulerpilot_no_quota.redis.csv`
- `eulerpilot_quota.redis.csv`
- `resource_control_events.jsonl`
- `agent.eulerpilot_no_quota.log`
- `agent.eulerpilot_quota.log`
- `summary.txt`
