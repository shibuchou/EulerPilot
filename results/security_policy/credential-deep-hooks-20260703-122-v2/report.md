# Security Policy Credential Deep Hooks

- result: `pass`
- reason: `deep-credential-hooks-configured-attached-and-scoped`
- host: `localhost.localdomain`
- kernel: `6.6.0-olk66-scx`
- runtime note: `no-userland-hit-observed`

## Purpose

This test evaluates the deeper credential lifecycle hooks
`lsm_cred_alloc_blank` and `lsm_cred_transfer` together with the stable
`lsm_cred_prepare` and `lsm_task_fix_setuid` evidence path. The Agent runs
in audit mode, scopes all credential rules to one lab cgroup, and verifies that
scope-only credential events map back to the correct YAML rule through the BPF
`hook_type` selector.

The test does not require ordinary userland workloads to hit
`cred_alloc_blank` or `cred_transfer`. Those hooks are treated as configured
and attached when the Agent starts successfully with both rules enabled; their
runtime hit counters are preserved in `deep_hook_status.txt`.

## Artifacts

- `summary.txt`
- `report.md`
- `agent.yaml`, `skills.yaml`
- `agent.log`
- `deep_hook_status.txt`
- `security_policy_events.credential-deep-anomaly.jsonl`
- `security_policy_events.credential-deep-hits.jsonl`
- `credential-*.out`, `credential-*.err`
