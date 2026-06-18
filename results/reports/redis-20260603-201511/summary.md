# Redis + stress-ng Benchmark

- timestamp: 2026-06-03T20:15:57+08:00
- redis port: 6380
- benchmark clients: 8
- benchmark requests: 2000
- stress workers: 1

Generated phases:

- `baseline`
- `default_noisy`
- `active_noisy`

Key outputs:

- `baseline_summary.csv`
- `default_noisy_summary.csv`
- `active_noisy_summary.csv`
- `compare_summary.csv`
- `active_noisy_agent_snapshot.txt`

This run compares the default noisy case and the EulerPilot active noisy case using the same Redis and stress-ng workload shape.
