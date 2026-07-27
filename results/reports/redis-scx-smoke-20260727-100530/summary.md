# Redis sched_ext Smoke

- timestamp: 2026-07-27T10:06:45+08:00
- redis port: 6384
- stress workers: 2
- benchmark clients: 16
- benchmark requests: 20000
- scx binary: /usr/local/bin/scx_eulerpilot

Generated outputs:

- `default_noisy_summary.csv`
- `active_noisy_sched_ext_summary.csv`
- `default_noisy_agent_snapshot.txt`
- `active_noisy_sched_ext_agent_snapshot.txt`
- `class_map_dump.json`
- `scx_stats_before.json`
- `scx_stats_after.json`
- `scx_stats_delta.json`
- `sched_ext_enable_seq.txt`
- `sched_ext_state.txt`

This smoke run is used to verify the `class_map -> scx_eulerpilot` control path before formal sched_ext experiments.
