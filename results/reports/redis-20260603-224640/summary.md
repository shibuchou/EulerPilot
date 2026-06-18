# Redis + stress-ng Benchmark

- timestamp: 2026-06-03T22:50:54+08:00
- redis port: 6380
- benchmark clients: 16
- benchmark requests: 10000
- stress workers: 2
- runs: 5

Generated phases per run:

- `baseline`
- `default_noisy`
- `active_noisy`

Top-level outputs:

- `compare_summary_avg.csv`
- `report.md`
- `run-*/compare_summary.csv`
- `run-*/active_noisy_agent_snapshot.txt`

This run compares the default noisy case and the EulerPilot active noisy case across multiple repetitions using the same Redis and stress-ng workload shape.
