# Security Policy Anomaly Process Filter

- result: `pass`
- reason: `security-anomaly-process-filter-observed`
- host: `localhost.localdomain`
- kernel: `6.6.0-olk66-scx`
- positive rule: `burst_openat_python_prefix`
- negative rule: `burst_openat_nohit_comm`

## Purpose

This test validates user-space process filtering for Security anomaly rules.
The same `/etc` openat burst is allowed to trigger only when the event comm
matches `comm_prefix=python`. The negative rule uses `comm=nohit-proc` and
must not emit an anomaly.

## Artifacts

- `summary.txt`
- `report.md`
- `agent.yaml`, `skills.yaml`
- `agent.log`
- `security_policy_events.anomaly-process-filter.jsonl`
