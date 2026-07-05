# Policy Engine Security -> Network + Resource Integration

- result: `pass`
- transaction_id: `pe-v3-1-1-1783257398`
- trigger: `security_policy anomaly/burst_connect`
- resource response: `cpu.max=20000 100000`, `memory.high=134217728`
- network response: `network_qos tc/tbf rate=2mbit`
- rollback: cgroup values restored and TBF qdisc removed

Evidence files in this directory include TC qdisc snapshots, rate probes, security/policy/network/resource events, and ActionJournal records.
