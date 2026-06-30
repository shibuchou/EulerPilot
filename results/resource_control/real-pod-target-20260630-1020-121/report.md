# Resource Control Real Pod Target

- result: `blocked`
- reason: `missing-kubectl`
- host: `localhost.localdomain`
- kernel: `6.6.0-132.0.0.111.oe2403sp3.x86_64`
- namespace: `eulerpilot-lab`
- pod: `eulerpilot-rc-pod`

## Purpose

This test validates the real Kubernetes Pod target path for `resource_control.target_ref`. When a lab Pod is available, EulerPilot resolves `type: k8s_pod` by Pod name, applies `cpu.max` and `memory.high` to the Pod cgroup, verifies audit events, and restores the old values after exit.

By default this script only uses an existing Pod. It creates `eulerpilot-lab/eulerpilot-rc-pod` only when `EULERPILOT_ALLOW_K8S_CREATE=1` is set.

## Artifacts

- `summary.txt`
- `commands.log`
- `agent.yaml`
- `skills.yaml`
- `agent.log`
- `resource_control.jsonl`
