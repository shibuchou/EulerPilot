# Security Policy Anomaly Combo Scope

- result: `pass`
- reason: `security-anomaly-combo-scope-observed`
- host: `localhost.localdomain`
- kernel: `6.6.0-132.0.0.111.oe2403sp3.x86_64`
- rule: `burst_openat_python_scoped_etc`

## Purpose

This test validates multi-dimensional anomaly filtering. The rule requires the
same event to match `syscall=openat`, `path_prefix=/etc`,
`comm_prefix=python`, and `target_ref=scoped_python_etc`. The test first
runs the same Python `/etc` open burst outside the cgroup and verifies that no
anomaly is emitted. It then runs the burst inside the scoped cgroup and verifies
that the emitted anomaly carries target scope evidence.

## Artifacts

- `summary.txt`
- `report.md`
- `agent.yaml`, `skills.yaml`
- `agent.log`
- `security_policy_events.anomaly-combo-scope.jsonl`
- `anomaly_combo_scope_summary.txt`
