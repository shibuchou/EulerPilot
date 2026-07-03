# Security Policy Anomaly Rules

- result: `pass`
- reason: `service-linkage-anomaly-rules-observed`
- host: `localhost.localdomain`
- kernel: `6.6.0-132.0.0.111.oe2403sp3.x86_64`
- rules: `burst_connect`, `burst_openat_sensitive`, `capability_abuse`

## Purpose

This test validates the service-linkage anomaly rules used by EulerPilot's
Security Policy skill. It runs the Agent in audit mode, triggers local connect
bursts, sensitive `/etc` openat bursts, and scoped CAP_SYS_ADMIN capable events,
then checks that all three anomaly events are written to
`reports/events/security_policy.jsonl`.

## Artifacts

- `summary.txt`
- `report.md`
- `agent.yaml`, `skills.yaml`
- `agent.log`
- `security_policy_events.anomaly-rules.jsonl`
- `capability-*.out`, `capability-*.err`
