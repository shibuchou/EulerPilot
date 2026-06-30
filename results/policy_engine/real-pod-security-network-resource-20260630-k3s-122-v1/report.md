# Policy Engine Real Pod Security -> Network + Resource

- result: `pass`
- reason: `real-pod-security-network-resource-applied-and-restored`
- host: `localhost.localdomain`
- kernel: `6.6.0-olk66-scx`
- namespace: `eulerpilot-lab`
- pod: `eulerpilot-rc-pod`
- cgroup: `/sys/fs/cgroup/kubepods/besteffort/pod03a42fb7-0c29-4c32-9dde-bd5367b9cfcc`
- host veth: `vethc59976b2`

## Purpose

This test validates the real Kubernetes Pod target path for the second Policy Engine cross-skill chain. A single YAML target uses `type: k8s_pod`; Policy Engine resolves it to the Pod cgroup for `resource_control` actions and to the Pod host veth for `network_qos` actions.

## Evidence

- `summary.txt`
- `commands.log`
- `agent.yaml`, `skills.yaml`
- `resolve_policy_pod_target.txt`
- `tc_qdisc_before.txt`, `tc_qdisc_after.txt`, `tc_qdisc_rollback.txt`
- `security_policy_events.jsonl`
- `policy_engine_events.jsonl`
- `resource_control_events.jsonl`
- `network_policy_events.jsonl`
- `action_journal.jsonl`
