# Resource Control Runtime Readiness

- result: `ready`
- reason: `runtime-ready`
- host: `localhost.localdomain`
- kernel: `6.6.0-olk66-scx`

## Runtime Probes

| Item | Value |
|------|-------|
| docker command | `/usr/bin/docker` |
| podman command | `/usr/bin/podman` |
| isula command | `missing` |
| nerdctl command | `missing` |
| ctr command | `/usr/local/bin/ctr` |
| crictl command | `/usr/local/bin/crictl` |
| kubectl command | `/usr/bin/kubectl` |
| docker service | `failed` |
| containerd service | `inactive` |
| crio service | `inactive` |
| isulad service | `inactive` |
| docker socket | `present` |
| containerd socket | `missing` |
| crio socket | `missing` |
| isulad socket | `missing` |
| runtime cgroup count | `1` |

## Interpretation

This diagnostic is intentionally read-only. It records whether the host can run a real container or Kubernetes target validation for `resource_control.target_ref`. When `container_runtime_ready=0`, the existing fake-runtime integration test remains the functional regression gate, and the next step is to install or start a real docker/podman/iSulad/containerd/cri-o runtime or provide an `eulerpilot-lab` Kubernetes namespace with a demo Pod.

## Artifacts

- `summary.txt`
- `commands.log`
- `runtime_cgroups.txt`
- `docker_ps.txt`, `podman_ps.txt`, `isula_ps.txt`, `crictl_ps.txt`, `ctr_list.txt`
- `kubectl_get_ns.txt`
