# Redis + stress-ng Benchmark

- timestamp: 2026-06-03T23:03:20+08:00
- redis port: 6380
- benchmark clients: 8
- benchmark requests: 800
- stress workers: 1
- runs: 1

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
