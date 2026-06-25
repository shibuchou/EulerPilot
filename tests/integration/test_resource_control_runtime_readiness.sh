#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR"

RESULT_DIR="${RESULT_DIR:-results/resource_control/runtime-readiness-$(date +%Y%m%d-%H%M%S)}"

mkdir -p "$RESULT_DIR"
: > "$RESULT_DIR/commands.log"
: > "$RESULT_DIR/summary.txt"

log_cmd() {
    printf '$ %s\n' "$*" >> "$RESULT_DIR/commands.log"
    "$@" >> "$RESULT_DIR/commands.log" 2>&1
}

command_path() {
    local cmd="$1"
    command -v "$cmd" 2>/dev/null || printf 'missing'
}

systemd_state() {
    local unit="$1"
    local state
    if ! command -v systemctl >/dev/null 2>&1; then
        printf 'unknown'
        return 0
    fi
    state="$(systemctl is-active "$unit" 2>/dev/null || true)"
    if [ -n "$state" ]; then
        printf '%s' "$state"
    else
        printf 'inactive'
    fi
}

socket_state() {
    local path="$1"
    if [ -S "$path" ]; then
        printf 'present'
    else
        printf 'missing'
    fi
}

run_probe() {
    local name="$1"
    shift
    local out="$RESULT_DIR/$name.txt"
    printf '$ %s\n' "$*" > "$out"
    if timeout 8s "$@" >> "$out" 2>&1; then
        printf '0'
    else
        printf '%s' "$?"
    fi
}

has_command() {
    command -v "$1" >/dev/null 2>&1
}

line_count() {
    local file="$1"
    if [ -f "$file" ]; then
        wc -l < "$file" | tr -d ' '
    else
        printf '0'
    fi
}

write_summary() {
    local result="$1"
    local reason="$2"
    {
        printf 'result=%s\n' "$result"
        printf 'reason=%s\n' "$reason"
        printf 'host=%s\n' "$(hostname 2>/dev/null || printf unknown)"
        printf 'date=%s\n' "$(date -Is)"
        printf 'kernel=%s\n' "$(uname -r)"
        printf 'container_runtime_ready=%s\n' "$CONTAINER_RUNTIME_READY"
        printf 'kubernetes_ready=%s\n' "$KUBERNETES_READY"
        printf 'runtime_cgroup_count=%s\n' "$RUNTIME_CGROUP_COUNT"
        printf 'docker_command=%s\n' "$DOCKER_CMD"
        printf 'podman_command=%s\n' "$PODMAN_CMD"
        printf 'nerdctl_command=%s\n' "$NERDCTL_CMD"
        printf 'ctr_command=%s\n' "$CTR_CMD"
        printf 'crictl_command=%s\n' "$CRICTL_CMD"
        printf 'kubectl_command=%s\n' "$KUBECTL_CMD"
        printf 'docker_service=%s\n' "$DOCKER_SERVICE"
        printf 'containerd_service=%s\n' "$CONTAINERD_SERVICE"
        printf 'crio_service=%s\n' "$CRIO_SERVICE"
        printf 'docker_socket=%s\n' "$DOCKER_SOCKET"
        printf 'containerd_socket=%s\n' "$CONTAINERD_SOCKET"
        printf 'crio_socket=%s\n' "$CRIO_SOCKET"
        printf 'docker_ps_rc=%s\n' "$DOCKER_PS_RC"
        printf 'podman_ps_rc=%s\n' "$PODMAN_PS_RC"
        printf 'crictl_ps_rc=%s\n' "$CRICTL_PS_RC"
        printf 'ctr_list_rc=%s\n' "$CTR_LIST_RC"
        printf 'kubectl_get_ns_rc=%s\n' "$KUBECTL_GET_NS_RC"
        printf 'next_action=%s\n' "$NEXT_ACTION"
    } > "$RESULT_DIR/summary.txt"
}

write_report() {
    cat > "$RESULT_DIR/report.md" <<EOF_REPORT
# Resource Control Runtime Readiness

- result: \`$(awk -F= '$1=="result"{print $2}' "$RESULT_DIR/summary.txt")\`
- reason: \`$(awk -F= '$1=="reason"{print $2}' "$RESULT_DIR/summary.txt")\`
- host: \`$(hostname 2>/dev/null || printf unknown)\`
- kernel: \`$(uname -r)\`

## Runtime Probes

| Item | Value |
|------|-------|
| docker command | \`$DOCKER_CMD\` |
| podman command | \`$PODMAN_CMD\` |
| nerdctl command | \`$NERDCTL_CMD\` |
| ctr command | \`$CTR_CMD\` |
| crictl command | \`$CRICTL_CMD\` |
| kubectl command | \`$KUBECTL_CMD\` |
| docker service | \`$DOCKER_SERVICE\` |
| containerd service | \`$CONTAINERD_SERVICE\` |
| crio service | \`$CRIO_SERVICE\` |
| docker socket | \`$DOCKER_SOCKET\` |
| containerd socket | \`$CONTAINERD_SOCKET\` |
| crio socket | \`$CRIO_SOCKET\` |
| runtime cgroup count | \`$RUNTIME_CGROUP_COUNT\` |

## Interpretation

This diagnostic is intentionally read-only. It records whether the host can run a real container or Kubernetes target validation for \`resource_control.target_ref\`. When \`container_runtime_ready=0\`, the existing fake-runtime integration test remains the functional regression gate, and the next step is to install or start a real docker/podman/containerd/cri-o runtime or provide an \`eulerpilot-lab\` Kubernetes namespace with a demo Pod.

## Artifacts

- \`summary.txt\`
- \`commands.log\`
- \`runtime_cgroups.txt\`
- \`docker_ps.txt\`, \`podman_ps.txt\`, \`crictl_ps.txt\`, \`ctr_list.txt\`
- \`kubectl_get_ns.txt\`
EOF_REPORT
}

DOCKER_CMD="$(command_path docker)"
PODMAN_CMD="$(command_path podman)"
NERDCTL_CMD="$(command_path nerdctl)"
CTR_CMD="$(command_path ctr)"
CRICTL_CMD="$(command_path crictl)"
KUBECTL_CMD="$(command_path kubectl)"

DOCKER_SERVICE="$(systemd_state docker)"
CONTAINERD_SERVICE="$(systemd_state containerd)"
CRIO_SERVICE="$(systemd_state crio)"

DOCKER_SOCKET="$(socket_state /var/run/docker.sock)"
CONTAINERD_SOCKET="$(socket_state /run/containerd/containerd.sock)"
CRIO_SOCKET="$(socket_state /var/run/crio/crio.sock)"

find /sys/fs/cgroup -maxdepth 5 -type d \
    \( -name '*docker*' -o -name '*kubepods*' -o -name '*containerd*' -o -name '*crio*' \) \
    2>/dev/null | sort > "$RESULT_DIR/runtime_cgroups.txt" || true
RUNTIME_CGROUP_COUNT="$(line_count "$RESULT_DIR/runtime_cgroups.txt")"

DOCKER_PS_RC="127"
PODMAN_PS_RC="127"
CRICTL_PS_RC="127"
CTR_LIST_RC="127"
KUBECTL_GET_NS_RC="127"

if has_command docker; then
    DOCKER_PS_RC="$(run_probe docker_ps docker ps --format '{{.ID}} {{.Names}} {{.State}}')"
fi
if has_command podman; then
    PODMAN_PS_RC="$(run_probe podman_ps podman ps --format '{{.ID}} {{.Names}} {{.Status}}')"
fi
if has_command crictl; then
    CRICTL_PS_RC="$(run_probe crictl_ps crictl ps -a)"
fi
if has_command ctr; then
    CTR_LIST_RC="$(run_probe ctr_list ctr -n k8s.io containers list)"
    if [ "$CTR_LIST_RC" != "0" ]; then
        CTR_LIST_RC="$(run_probe ctr_list_default ctr containers list)"
    fi
fi
if has_command kubectl; then
    KUBECTL_GET_NS_RC="$(run_probe kubectl_get_ns kubectl get namespace eulerpilot-lab)"
fi

CONTAINER_RUNTIME_READY=0
if [ "$DOCKER_PS_RC" = "0" ] || [ "$PODMAN_PS_RC" = "0" ] ||
   [ "$CRICTL_PS_RC" = "0" ] || [ "$CTR_LIST_RC" = "0" ]; then
    CONTAINER_RUNTIME_READY=1
fi

KUBERNETES_READY=0
if [ "$KUBECTL_GET_NS_RC" = "0" ]; then
    KUBERNETES_READY=1
fi

NEXT_ACTION="none"
RESULT="ready"
REASON="runtime-ready"
if [ "$CONTAINER_RUNTIME_READY" = "0" ] && [ "$KUBERNETES_READY" = "0" ]; then
    RESULT="blocked"
    REASON="missing-container-runtime-and-kubernetes-lab"
    NEXT_ACTION="install-or-start-docker-podman-containerd-crio-or-provide-eulerpilot-lab-pod"
elif [ "$CONTAINER_RUNTIME_READY" = "0" ]; then
    RESULT="partial"
    REASON="kubernetes-visible-but-container-runtime-unavailable"
    NEXT_ACTION="enable-runtime-cli-or-cri-socket"
elif [ "$KUBERNETES_READY" = "0" ]; then
    RESULT="partial"
    REASON="container-runtime-ready-kubernetes-lab-missing"
    NEXT_ACTION="create-eulerpilot-lab-namespace-and-demo-pod"
fi

log_cmd uname -a || true
log_cmd mount || true
log_cmd cat /proc/self/cgroup || true

write_summary "$RESULT" "$REASON"
write_report

printf '[INFO] resource_control runtime readiness result saved to %s\n' "$RESULT_DIR"
cat "$RESULT_DIR/summary.txt"
