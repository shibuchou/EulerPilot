# Security Policy Credential Anomaly

- result: `pass`
- reason: `credential-churn-anomaly-observed`
- host: `localhost.localdomain`
- kernel: `6.6.0-132.0.0.111.oe2403sp3.x86_64`
- rule: `credential_churn`

## Purpose

This test validates the Security Policy credential lifecycle anomaly rule. It
runs the Agent in audit mode, scopes `lsm_cred_prepare` and
`lsm_task_fix_setuid` to a lab cgroup, triggers repeated credential
transitions with `setuid(65534)`, and checks that a `credential_churn`
anomaly includes lifecycle evidence such as `credential_stage` plus `uid`
or `cred_gfp`, depending on the hook that crosses the anomaly threshold.

## Artifacts

- `summary.txt`
- `report.md`
- `agent.yaml`, `skills.yaml`
- `agent.log`
- `security_policy_events.credential-anomaly.jsonl`
- `security_policy_events.credential-hits.jsonl`
- `credential-*.out`, `credential-*.err`
