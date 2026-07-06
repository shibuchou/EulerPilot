# Redis sched_ext PSI ACTIVE Probe

- timestamp: 2026-07-06T10:09:21+08:00
- redis port: 6390
- stress workers: 4
- bench clients: 64
- bench requests: 30000
- bench tests: set,get,incr
- scx binary: /usr/local/bin/scx_eulerpilot
- result: PSI gate entered ACTIVE

Evidence files:

- `agent_snapshot.txt`
- `psi_gate_trace.jsonl`
- `gate_status.txt`
- `scx_stats.json`
- `redis_benchmark.csv`
- `stress.log`
