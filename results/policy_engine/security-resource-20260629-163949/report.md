# Policy Engine Security -> Resource Control Integration

- result: `pass`
- source: `security_policy anomaly/burst_execve`
- response: `policy_engine cross_skill_response`
- target cgroup: `/sys/fs/cgroup/eulerpilot/policy-engine-background`
- cpu.max response: `10000 100000`
- memory.high response: `1048576`

The test verifies that a Security anomaly event can trigger a Resource Control downgrade action through the unified Agent process. It also verifies ActionJournal evidence and rollback to the old cgroup values after Agent exit.
