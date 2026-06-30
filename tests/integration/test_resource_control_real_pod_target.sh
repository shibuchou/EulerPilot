#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR"

RESULT_DIR="${RESULT_DIR:-results/resource_control/real-pod-target-$(date +%Y%m%d-%H%M%S)}"
RESULT_ABS_DIR=""
KUBECTL_BIN="${EULERPILOT_KUBECTL_BIN:-kubectl}"
POD_NAMESPACE="${EULERPILOT_POD_NAMESPACE:-eulerpilot-lab}"
POD_NAME="${EULERPILOT_POD_NAME:-eulerpilot-rc-pod}"
POD_CONTAINER_NAME="${EULERPILOT_POD_CONTAINER_NAME:-}"
POD_IMAGE="${EULERPILOT_POD_IMAGE:-busybox:latest}"
POD_IMAGE_PULL_POLICY="${EULERPILOT_POD_IMAGE_PULL_POLICY:-IfNotPresent}"
ALLOW_K8S_CREATE="${EULERPILOT_ALLOW_K8S_CREATE:-0}"
AGENT_DURATION_S="${EULERPILOT_REAL_POD_AGENT_DURATION_S:-3}"
AGENT_TIMEOUT_S="${EULERPILOT_REAL_POD_AGENT_TIMEOUT_S:-15}"

AGENT_PID=""
POD_CREATED=0
NS_CREATED=0
TARGET_CGROUP=""
OLD_CPU_MAX=""
OLD_MEMORY_HIGH=""

mkdir -p "$RESULT_DIR" reports/events run/eulerpilot
: > "$RESULT_DIR/commands.log"
: > "$RESULT_DIR/summary.txt"

info() {
    printf '[INFO] %s\n' "$*"
}

fail() {
    printf '[FAIL] %s\n' "$*" >&2
    exit 1
}

log_cmd() {
    printf '$ %s\n' "$*" >> "$RESULT_DIR/commands.log"
    "$@" >> "$RESULT_DIR/commands.log" 2>&1
}

command_path() {
    command -v "$1" 2>/dev/null || true
}

write_kv() {
    printf '%s=%s\n' "$1" "$2" >> "$RESULT_DIR/summary.txt"
}

write_blocked() {
    local reason="$1"
    local next_action="$2"
    {
        printf 'result=blocked\n'
        printf 'reason=%s\n' "$reason"
        printf 'host=%s\n' "$(hostname 2>/dev/null || printf unknown)"
        printf 'date=%s\n' "$(date -Is)"
        printf 'kernel=%s\n' "$(uname -r)"
        printf 'kubectl_bin=%s\n' "$KUBECTL_BIN"
        printf 'kubectl_command=%s\n' "$(command_path "$KUBECTL_BIN")"
        printf 'pod_namespace=%s\n' "$POD_NAMESPACE"
        printf 'pod_name=%s\n' "$POD_NAME"
        printf 'pod_container_name=%s\n' "$POD_CONTAINER_NAME"
        printf 'pod_image=%s\n' "$POD_IMAGE"
        printf 'pod_image_pull_policy=%s\n' "$POD_IMAGE_PULL_POLICY"
        printf 'next_action=%s\n' "$next_action"
    } > "$RESULT_DIR/summary.txt"
}

write_report() {
    local result reason
    result="$(awk -F= '$1=="result"{print $2}' "$RESULT_DIR/summary.txt" 2>/dev/null || true)"
    reason="$(awk -F= '$1=="reason"{print $2}' "$RESULT_DIR/summary.txt" 2>/dev/null || true)"
    cat > "$RESULT_DIR/report.md" <<EOF_REPORT
# Resource Control Real Pod Target

- result: \`${result:-unknown}\`
- reason: \`${reason:-unknown}\`
- host: \`$(hostname 2>/dev/null || printf unknown)\`
- kernel: \`$(uname -r)\`
- namespace: \`${POD_NAMESPACE}\`
- pod: \`${POD_NAME}\`
- image pull policy: \`${POD_IMAGE_PULL_POLICY}\`

## Purpose

This test validates the real Kubernetes Pod target path for \`resource_control.target_ref\`. When a lab Pod is available, EulerPilot resolves \`type: k8s_pod\` by Pod name, applies \`cpu.max\` and \`memory.high\` to the Pod cgroup, verifies audit events, and restores the old values after exit.

By default this script only uses an existing Pod. It creates \`eulerpilot-lab/${POD_NAME}\` only when \`EULERPILOT_ALLOW_K8S_CREATE=1\` is set.

## Artifacts

- \`summary.txt\`
- \`commands.log\`
- \`agent.yaml\`
- \`skills.yaml\`
- \`agent.log\`
- \`resource_control.jsonl\`
EOF_REPORT
}

cleanup() {
    set +e
    if [ -n "$AGENT_PID" ] && kill -0 "$AGENT_PID" 2>/dev/null; then
        kill "$AGENT_PID" 2>/dev/null
        wait "$AGENT_PID" 2>/dev/null
    fi
    if [ -n "$TARGET_CGROUP" ]; then
        if [ -n "$OLD_CPU_MAX" ] && [ -w "$TARGET_CGROUP/cpu.max" ]; then
            printf '%s\n' "$OLD_CPU_MAX" > "$TARGET_CGROUP/cpu.max" 2>/dev/null || true
        fi
        if [ -n "$OLD_MEMORY_HIGH" ] && [ -w "$TARGET_CGROUP/memory.high" ]; then
            printf '%s\n' "$OLD_MEMORY_HIGH" > "$TARGET_CGROUP/memory.high" 2>/dev/null || true
        fi
    fi
    scripts/rollback.sh >> "$RESULT_DIR/commands.log" 2>&1 || true
    if [ "$POD_CREATED" = "1" ]; then
        "$KUBECTL_BIN" -n "$POD_NAMESPACE" delete pod "$POD_NAME" \
            --ignore-not-found=true >> "$RESULT_DIR/commands.log" 2>&1 || true
    fi
    if [ "$NS_CREATED" = "1" ]; then
        "$KUBECTL_BIN" delete namespace "$POD_NAMESPACE" \
            --ignore-not-found=true >> "$RESULT_DIR/commands.log" 2>&1 || true
    fi
}
trap cleanup EXIT

wait_for_value() {
    local file="$1"
    local expected="$2"
    local deadline=$((SECONDS + 20))
    while [ "$SECONDS" -lt "$deadline" ]; do
        if [ -f "$file" ] && [ "$(cat "$file")" = "$expected" ]; then
            return 0
        fi
        sleep 0.2
    done
    printf '[DEBUG] %s now=%s expected=%s\n' \
        "$file" "$(cat "$file" 2>/dev/null || true)" "$expected" >&2
    return 1
}

find_pod_cgroup() {
    local pod_uid="$1"
    local uid_systemd uid_compact
    uid_systemd="${pod_uid//-/_}"
    uid_compact="${pod_uid//-/}"
    find /sys/fs/cgroup -maxdepth 12 -type d \
        \( -path "*$pod_uid*" -o -path "*$uid_systemd*" -o -path "*$uid_compact*" \) \
        2>/dev/null | awk '{ print length($0), $0 }' | sort -n | sed -n '1s/^[0-9][0-9]* //p'
}

write_agent_config() {
    local container_name_yaml=""
    if [ -n "$POD_CONTAINER_NAME" ]; then
        container_name_yaml="        container_name: $POD_CONTAINER_NAME"
    fi

    cat > "$RESULT_DIR/agent.yaml" <<YAML
agent:
  name: EulerPilot
  mode: active
skills_config_path: $RESULT_ABS_DIR/skills.yaml
scheduler:
  type: cgroup_v2
exporter:
  prometheus:
    enabled: false
YAML

    cat > "$RESULT_DIR/skills.yaml" <<YAML
schema_version: 2
skills:
- name: resource_control
  kind: runtime
  enabled: true
  config:
    mode: enforce
    controllers:
      cpu:
        max:
          enabled: true
      memory:
        enabled: true
        high:
          enabled: true
        low:
          enabled: true
        max:
          enabled: true
        reclaim:
          enabled: false
      io:
        enabled: false
        weight:
          enabled: false
        max:
          enabled: false
    targets:
      lab_pod:
        type: k8s_pod
        namespace: $POD_NAMESPACE
        pod_name: $POD_NAME
$container_name_yaml
        kubectl_path: $KUBECTL_BIN
        lab_namespace: eulerpilot-lab
        require_runtime_socket: false
    profiles:
      background:
        target_ref: lab_pod
        normal:
          cpu_max: max
          memory_high: max
        pressure:
          target_ref: lab_pod
          cpu_max: '10000 100000'
          memory_high: '1048576'
          memory_low: '0'
          memory_max: max
- name: psi_gate
  kind: runtime
  enabled: true
  config: {}
YAML
}

[ "$(id -u)" -eq 0 ] || fail 'real pod target test must run as root'

RESULT_ABS_DIR="$(cd "$RESULT_DIR" && pwd)"

if ! command -v "$KUBECTL_BIN" >/dev/null 2>&1; then
    write_blocked "missing-kubectl" \
        "install-kubectl-and-provide-eulerpilot-lab-demo-pod"
    write_report
    info "result directory: $RESULT_DIR"
    exit 0
fi

if ! timeout 8s "$KUBECTL_BIN" get namespace "$POD_NAMESPACE" \
    > "$RESULT_DIR/kubectl_get_namespace.txt" 2>&1; then
    if [ "$ALLOW_K8S_CREATE" != "1" ]; then
        write_blocked "lab-namespace-missing" \
            "create-eulerpilot-lab-namespace-or-set-EULERPILOT_ALLOW_K8S_CREATE=1"
        write_report
        info "result directory: $RESULT_DIR"
        exit 0
    fi
    log_cmd "$KUBECTL_BIN" create namespace "$POD_NAMESPACE"
    NS_CREATED=1
fi

if ! timeout 8s "$KUBECTL_BIN" -n "$POD_NAMESPACE" get pod "$POD_NAME" \
    > "$RESULT_DIR/kubectl_get_pod.txt" 2>&1; then
    if [ "$ALLOW_K8S_CREATE" != "1" ]; then
        write_blocked "lab-pod-missing" \
            "create-demo-pod-or-set-EULERPILOT_ALLOW_K8S_CREATE=1"
        write_report
        info "result directory: $RESULT_DIR"
        exit 0
    fi
    log_cmd "$KUBECTL_BIN" -n "$POD_NAMESPACE" run "$POD_NAME" \
        --image="$POD_IMAGE" --image-pull-policy="$POD_IMAGE_PULL_POLICY" \
        --restart=Never --command -- sh -c 'yes >/dev/null'
    POD_CREATED=1
fi

if ! timeout 70s "$KUBECTL_BIN" -n "$POD_NAMESPACE" wait \
    --for=condition=Ready "pod/$POD_NAME" --timeout=60s \
    > "$RESULT_DIR/kubectl_wait_pod.txt" 2>&1; then
    write_blocked "lab-pod-not-ready" \
        "check-pod-image-runtime-and-cluster-events"
    write_report
    info "result directory: $RESULT_DIR"
    exit 0
fi

POD_UID="$("$KUBECTL_BIN" -n "$POD_NAMESPACE" get pod "$POD_NAME" \
    -o 'jsonpath={.metadata.uid}' | tr -d '[:space:]')"
if [ -z "$POD_UID" ]; then
    write_blocked "pod-uid-query-failed" \
        "check-kubectl-access-to-demo-pod"
    write_report
    info "result directory: $RESULT_DIR"
    exit 0
fi

TARGET_CGROUP="$(find_pod_cgroup "$POD_UID")"
if [ -z "$TARGET_CGROUP" ] || [ ! -d "$TARGET_CGROUP" ]; then
    write_blocked "pod-cgroup-not-found" \
        "check-kubelet-cgroup-driver-and-cgroup-v2-layout"
    write_kv "pod_uid" "$POD_UID"
    write_report
    info "result directory: $RESULT_DIR"
    exit 0
fi

if [ ! -w "$TARGET_CGROUP/cpu.max" ] || [ ! -w "$TARGET_CGROUP/memory.high" ]; then
    write_blocked "pod-cgroup-not-writable" \
        "enable-cgroup-v2-cpu-memory-controllers-for-kubelet-slice"
    write_kv "pod_uid" "$POD_UID"
    write_kv "target_cgroup" "$TARGET_CGROUP"
    write_report
    info "result directory: $RESULT_DIR"
    exit 0
fi

if [ ! -x ./build/eulerpilot-agent ]; then
    log_cmd make agent
fi

: > reports/events/resource_control.jsonl
: > run/eulerpilot/action_journal.jsonl

OLD_CPU_MAX="$(cat "$TARGET_CGROUP/cpu.max")"
OLD_MEMORY_HIGH="$(cat "$TARGET_CGROUP/memory.high")"
write_agent_config

timeout "${AGENT_TIMEOUT_S}s" ./build/eulerpilot-agent \
    --config "$RESULT_DIR/agent.yaml" \
    --backend cgroup_v2 \
    --gate-mode always-active \
    --active \
    --duration-s "$AGENT_DURATION_S" \
    --interval-ms 500 \
    --jsonl \
    > "$RESULT_DIR/agent.log" 2>&1 &
AGENT_PID="$!"

wait_for_value "$TARGET_CGROUP/cpu.max" '10000 100000' ||
    fail "real Pod cpu.max pressure value was not applied"
wait_for_value "$TARGET_CGROUP/memory.high" '1048576' ||
    fail "real Pod memory.high pressure value was not applied"

wait "$AGENT_PID"
AGENT_PID=""

wait_for_value "$TARGET_CGROUP/cpu.max" "$OLD_CPU_MAX" ||
    fail "real Pod cpu.max was not restored"
wait_for_value "$TARGET_CGROUP/memory.high" "$OLD_MEMORY_HIGH" ||
    fail "real Pod memory.high was not restored"

cp reports/events/resource_control.jsonl "$RESULT_DIR/resource_control.jsonl"
grep -q '"target_ref":"lab_pod"' "$RESULT_DIR/resource_control.jsonl" ||
    fail "resource_control audit log missing lab_pod target_ref"
grep -q '"file":"cpu.max"' "$RESULT_DIR/resource_control.jsonl" ||
    fail "resource_control audit log missing cpu.max"
grep -q '"file":"memory.high"' "$RESULT_DIR/resource_control.jsonl" ||
    fail "resource_control audit log missing memory.high"
grep -q '"result":"applied"' "$RESULT_DIR/resource_control.jsonl" ||
    fail "resource_control audit log missing applied event"
grep -q '"result":"restored"' "$RESULT_DIR/resource_control.jsonl" ||
    fail "resource_control audit log missing restored event"

{
    printf 'result=pass\n'
    printf 'reason=real-pod-target-applied-and-restored\n'
    printf 'host=%s\n' "$(hostname 2>/dev/null || printf unknown)"
    printf 'date=%s\n' "$(date -Is)"
    printf 'kernel=%s\n' "$(uname -r)"
    printf 'kubectl_bin=%s\n' "$KUBECTL_BIN"
    printf 'pod_namespace=%s\n' "$POD_NAMESPACE"
    printf 'pod_name=%s\n' "$POD_NAME"
    printf 'pod_container_name=%s\n' "$POD_CONTAINER_NAME"
    printf 'pod_uid=%s\n' "$POD_UID"
    printf 'target_ref=lab_pod\n'
    printf 'target_cgroup=%s\n' "$TARGET_CGROUP"
    printf 'cpu_max_pressure=10000 100000\n'
    printf 'memory_high_pressure=1048576\n'
    printf 'old_cpu_max=%s\n' "$OLD_CPU_MAX"
    printf 'old_memory_high=%s\n' "$OLD_MEMORY_HIGH"
} > "$RESULT_DIR/summary.txt"

write_report
info "result directory: $RESULT_DIR"
