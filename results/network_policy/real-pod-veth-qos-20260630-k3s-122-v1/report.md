# Network QoS Real Pod veth Target

- result: `pass`
- reason: `real-pod-veth-qos-applied-and-restored`
- host: `localhost.localdomain`
- kernel: `6.6.0-olk66-scx`
- namespace: `eulerpilot-lab`
- pod: `eulerpilot-rc-pod`
- rate: `1mbit`

## Purpose

This test validates the real Kubernetes Pod host-veth path for `network_qos.target_ref`. It resolves `type: k8s_pod` in the lab namespace, attaches TC/TBF to the resolved host veth, sends local host-to-Pod traffic, verifies packet-hit evidence, and checks rollback removes qdisc state.

The script only allows the `eulerpilot-lab` namespace by default. It creates the namespace or Pod only when `EULERPILOT_ALLOW_K8S_CREATE=1` is set.

## Artifacts

- `summary.txt`
- `commands.log`
- `agent.yaml`
- `skills.yaml`
- `resolve_pod_veth.txt`
- `tc_qdisc_before.txt`, `tc_qdisc_after.txt`, `tc_qdisc_rollback.txt`
- `network_policy.jsonl`
- `action_journal.jsonl`
