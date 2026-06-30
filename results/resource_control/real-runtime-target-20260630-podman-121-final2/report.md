# Resource Control Real Runtime Target

- result: `pass`
- reason: `real-runtime-target-applied-and-restored`
- host: `localhost.localdomain`
- kernel: `6.6.0-132.0.0.111.oe2403sp3.x86_64`
- runtime: `podman`
- image: `localhost/eulerpilot-busybox:latest`

## Purpose

This test is the real-runtime companion of `test_resource_control_runtime_target.sh`. It runs a real docker, podman, or iSulad/isula container when a local runtime and image are available, configures `resource_control.target_ref` with `type: container`, and verifies that EulerPilot applies and restores `cpu.max` and `memory.high` on the resolved container cgroup.

When docker/podman/isula or the requested image is missing, the script exits with `result=blocked` and records the exact next action instead of silently installing packages or pulling images.

## Artifacts

- `summary.txt`
- `commands.log`
- `agent.yaml`
- `skills.yaml`
- `agent.log`
- `resource_control.jsonl`
