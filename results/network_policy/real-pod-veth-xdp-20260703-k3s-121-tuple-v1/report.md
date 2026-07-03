# Network XDP Real Pod veth Target

- result: `pass`
- reason: `real-pod-veth-xdp-attached-dropped-and-restored`
- host: `localhost.localdomain`
- kernel: `6.6.0-132.0.0.111.oe2403sp3.x86_64`
- namespace: `eulerpilot-lab`
- pod: `eulerpilot-rc-pod`
- host veth: `veth998e0158`
- xdp drop count: `21`
- xdp TCP drop count: `4`
- xdp UDP drop count: `8`
- xdp UDP tuple drop count: `8`

## Purpose

This test validates the real Kubernetes Pod host-veth path for `network_xdp.target_ref`.
It resolves `type: k8s_pod` in the lab namespace, attaches generic XDP to the resolved
host veth, sends Pod-to-host traffic, verifies XDP drop evidence, and checks rollback
detaches the XDP program.

The script only allows the `eulerpilot-lab` namespace by default. It creates the
namespace or Pod only when `EULERPILOT_ALLOW_K8S_CREATE=1` is set.

## Artifacts

- `summary.txt`
- `commands.log`
- `agent.audit.yaml`, `skills.audit.yaml`
- `agent.enforce.yaml`, `skills.enforce.yaml`
- `resolve_pod_veth.txt`
- `xdp_link_before.txt`, `xdp_link_audit.txt`, `xdp_link_enforce.txt`, `xdp_link_rollback.txt`
- `baseline_ping.txt`, `enforce_ping_drop.txt`, `rollback_ping.txt`
- `enforce_tcp_drop.txt`, `enforce_udp_drop.txt`, `xdp_rule_stats.txt`
- `network_policy.jsonl`
- `action_journal.jsonl`
