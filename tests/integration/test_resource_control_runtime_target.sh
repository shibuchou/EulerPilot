#!/usr/bin/env bash
set -euo pipefail

ROOT="/sys/fs/cgroup/eulerpilot"
RUNTIME_ROOT="$ROOT/runtime-targets"
RESULT_DIR="${RESULT_DIR:-results/resource_control/runtime-target-$(date +%Y%m%d-%H%M%S)}"
TMP_DIR="$(mktemp -d)"
AGENT_PID=""
TARGET_PID=""
OUTSIDE_PID=""
CONTAINER_ID="epcontainer$(date +%s)$$"
RUNTIME_CONTAINER_ID="epruntime$(date +%s)$$"
CONTAINER_NAME="ep-runtime-worker"
POD_UID="123e4567-e89b-12d3-a456-$(printf '%012d' "$$")"
POD_NAME="ep-web-demo"
POD_NAMESPACE="eulerpilot-lab"

fail() {
    printf '[FAIL] %s\n' "$*" >&2
    exit 1
}

info() {
    printf '[INFO] %s\n' "$*"
}

cleanup_process() {
    local pid="$1"
    if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    fi
}

cleanup() {
    set +e
    if [ -n "$AGENT_PID" ] && kill -0 "$AGENT_PID" 2>/dev/null; then
        kill "$AGENT_PID" 2>/dev/null
        wait "$AGENT_PID" 2>/dev/null
    fi
    cleanup_process "$TARGET_PID"
    cleanup_process "$OUTSIDE_PID"
    scripts/rollback.sh > "$RESULT_DIR/rollback.log" 2>&1
    find "$RUNTIME_ROOT" -depth -type d -exec rmdir {} \; >/dev/null 2>&1
    rmdir "$RUNTIME_ROOT" >/dev/null 2>&1 || true
    rm -rf "$TMP_DIR"
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

enable_controller() {
    local subtree="$1"
    local controller="$2"
    [ -w "$subtree/cgroup.subtree_control" ] || return 0
    if grep -qw "$controller" "$subtree/cgroup.controllers" 2>/dev/null; then
        echo "+$controller" > "$subtree/cgroup.subtree_control" 2>/dev/null || true
    fi
}

init_cgroup() {
    local path="$1"
    mkdir -p "$path"
    [ -w "$path/cpuset.mems" ] && echo 0 > "$path/cpuset.mems" || true
    [ -w "$path/cpuset.cpus" ] && echo 0-1 > "$path/cpuset.cpus" || true
    [ -w "$path/cpu.max" ] && echo max > "$path/cpu.max" || true
    [ -w "$path/memory.high" ] && echo max > "$path/memory.high" || true
    [ -w "$path/memory.low" ] && echo 0 > "$path/memory.low" || true
    [ -w "$path/memory.max" ] && echo max > "$path/memory.max" || true
}

init_parent_cgroup() {
    local path="$1"
    init_cgroup "$path"
    for controller in cpu cpuset memory io; do
        enable_controller "$path" "$controller"
    done
}

write_common_agent_config() {
    local scenario="$1"
    cat > "$RESULT_DIR/agent.$scenario.yaml" <<YAML
agent:
  name: EulerPilot
  mode: active
skills_config_path: skills.$scenario.yaml
scheduler:
  type: cgroup_v2
exporter:
  prometheus:
    enabled: false
YAML
}

write_skill_config() {
    local scenario="$1"
    local target_ref="$2"
    local target_yaml="$3"
    cat > "$RESULT_DIR/skills.$scenario.yaml" <<YAML
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
$target_yaml
    profiles:
      background:
        target_ref: $target_ref
        normal:
          cpu_max: max
          memory_high: max
        pressure:
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

run_runtime_scenario() {
    local scenario="$1"
    local target_ref="$2"
    local target_path="$3"
    local target_yaml="$4"
    local outside_path="$RUNTIME_ROOT/outside-$scenario"

    init_cgroup "$target_path"
    init_cgroup "$outside_path"
    [ -w "$target_path/cpu.max" ] || fail "$target_path/cpu.max is not writable"
    [ -w "$target_path/memory.high" ] || fail "$target_path/memory.high is not writable"
    [ -w "$outside_path/cpu.max" ] || fail "$outside_path/cpu.max is not writable"

    local old_cpu_max old_memory_high old_outside_cpu_max
    old_cpu_max="$(cat "$target_path/cpu.max")"
    old_memory_high="$(cat "$target_path/memory.high")"
    old_outside_cpu_max="$(cat "$outside_path/cpu.max")"

    yes > /dev/null &
    TARGET_PID="$!"
    echo "$TARGET_PID" > "$target_path/cgroup.procs"

    yes > /dev/null &
    OUTSIDE_PID="$!"
    echo "$OUTSIDE_PID" > "$outside_path/cgroup.procs"

    info "$scenario target pid=$TARGET_PID outside pid=$OUTSIDE_PID"

    write_common_agent_config "$scenario"
    write_skill_config "$scenario" "$target_ref" "$target_yaml"

    timeout 30s ./build/eulerpilot-agent \
        --config "$RESULT_DIR/agent.$scenario.yaml" \
        --backend cgroup_v2 \
        --gate-mode always-active \
        --active \
        --duration-s 12 \
        --interval-ms 500 \
        --jsonl \
        > "$RESULT_DIR/agent.$scenario.log" 2>&1 &
    AGENT_PID="$!"

    wait_for_value "$target_path/cpu.max" '10000 100000' ||
        fail "$scenario target cpu.max pressure value was not applied"
    wait_for_value "$target_path/memory.high" '1048576' ||
        fail "$scenario target memory.high pressure value was not applied"

    if [ "$(cat "$outside_path/cpu.max")" != "$old_outside_cpu_max" ]; then
        fail "$scenario outside cgroup cpu.max changed unexpectedly: $(cat "$outside_path/cpu.max")"
    fi

    if ! grep -qw "$TARGET_PID" "$target_path/cgroup.procs"; then
        fail "$scenario target workload left configured cgroup unexpectedly"
    fi
    if ! grep -qw "$OUTSIDE_PID" "$outside_path/cgroup.procs"; then
        fail "$scenario outside workload moved unexpectedly"
    fi

    wait "$AGENT_PID"
    AGENT_PID=""

    wait_for_value "$target_path/cpu.max" "$old_cpu_max" ||
        fail "$scenario target cpu.max was not restored after agent stop"
    wait_for_value "$target_path/memory.high" "$old_memory_high" ||
        fail "$scenario target memory.high was not restored after agent stop"

    grep -q "\"target_ref\":\"$target_ref\"" reports/events/resource_control.jsonl ||
        fail "$scenario audit log missing target_ref=$target_ref"
    grep -q "\"cgroup\":\"$target_path\"" reports/events/resource_control.jsonl ||
        fail "$scenario audit log missing target cgroup path"
    grep -q "\"target_ref\":\"$target_ref\"" "$RESULT_DIR/agent.$scenario.log" ||
        fail "$scenario agent jsonl missing target_ref"
    grep -q '"result":"applied"' reports/events/resource_control.jsonl ||
        fail "$scenario audit log missing applied result"
    grep -q '"result":"restored"' reports/events/resource_control.jsonl ||
        fail "$scenario audit log missing restored result"

    {
        printf '%s_target_ref=%s\n' "$scenario" "$target_ref"
        printf '%s_target_cgroup=%s\n' "$scenario" "$target_path"
        printf '%s_outside_cgroup=%s\n' "$scenario" "$outside_path"
        printf '%s_cpu_max_pressure=10000 100000\n' "$scenario"
        printf '%s_memory_high_pressure=1048576\n' "$scenario"
        printf '%s_old_cpu_max=%s\n' "$scenario" "$old_cpu_max"
        printf '%s_old_memory_high=%s\n' "$scenario" "$old_memory_high"
        printf '%s_outside_cpu_max=%s\n' "$scenario" "$old_outside_cpu_max"
    } >> "$RESULT_DIR/summary.txt"

    cleanup_process "$TARGET_PID"
    cleanup_process "$OUTSIDE_PID"
    TARGET_PID=""
    OUTSIDE_PID=""
}

[ "$(id -u)" -eq 0 ] || fail 'resource_control runtime target integration test must run as root'

mkdir -p "$RESULT_DIR" "$RUNTIME_ROOT" reports/events run/eulerpilot
: > "$RESULT_DIR/summary.txt"
: > reports/events/resource_control.jsonl
: > run/eulerpilot/action_journal.jsonl

make agent
scripts/setup_cgroup_v2.sh > "$RESULT_DIR/setup.log" 2>&1
init_parent_cgroup "$RUNTIME_ROOT"

FAKE_CRICTL="$TMP_DIR/crictl"
cat > "$FAKE_CRICTL" <<'SH'
#!/bin/sh
if [ "$1" = "ps" ]; then
    printf '%s\n' "${EULERPILOT_TEST_CONTAINER_ID:?}"
    exit 0
fi
if [ "$1" = "inspect" ]; then
    printf '{"info":{"pid":%s}}\n' "${EULERPILOT_TEST_CONTAINER_PID:-0}"
    exit 0
fi
exit 1
SH
chmod +x "$FAKE_CRICTL"

FAKE_KUBECTL="$TMP_DIR/kubectl"
cat > "$FAKE_KUBECTL" <<'SH'
#!/bin/sh
case "$*" in
    *metadata.uid*)
        printf '%s\n' "${EULERPILOT_TEST_POD_UID:?}"
        ;;
    *)
        exit 1
        ;;
esac
SH
chmod +x "$FAKE_KUBECTL"

export EULERPILOT_TEST_CONTAINER_ID="$RUNTIME_CONTAINER_ID"
export EULERPILOT_TEST_POD_UID="$POD_UID"

CONTAINER_ID_TARGET="$RUNTIME_ROOT/cri-containerd-$CONTAINER_ID.scope"
CONTAINER_NAME_TARGET="$RUNTIME_ROOT/container-name-$RUNTIME_CONTAINER_ID.scope"
POD_TARGET="$RUNTIME_ROOT/pod${POD_UID//-/_}.slice"

run_runtime_scenario \
    "container_id" \
    "container_id_scope" \
    "$CONTAINER_ID_TARGET" \
"      container_id_scope:
        type: container_id
        container_id: $CONTAINER_ID
        cgroup_root: $ROOT"

run_runtime_scenario \
    "container_name" \
    "container_name_scope" \
    "$CONTAINER_NAME_TARGET" \
"      container_name_scope:
        type: container
        container_name: $CONTAINER_NAME
        runtime: crictl
        crictl_path: $FAKE_CRICTL
        cgroup_root: $ROOT
        require_runtime_socket: false"

run_runtime_scenario \
    "k8s_pod" \
    "pod_scope" \
    "$POD_TARGET" \
"      pod_scope:
        type: k8s_pod
        namespace: $POD_NAMESPACE
        pod_name: $POD_NAME
        kubectl_path: $FAKE_KUBECTL
        cgroup_root: $ROOT
        lab_namespace: $POD_NAMESPACE
        require_runtime_socket: false"

cp reports/events/resource_control.jsonl "$RESULT_DIR/resource_control_events.jsonl"
{
    printf 'result=pass\n'
    printf 'target_types=container_id,container,k8s_pod\n'
    printf 'container_id=%s\n' "$CONTAINER_ID"
    printf 'runtime_container_id=%s\n' "$RUNTIME_CONTAINER_ID"
    printf 'container_name=%s\n' "$CONTAINER_NAME"
    printf 'pod_namespace=%s\n' "$POD_NAMESPACE"
    printf 'pod_name=%s\n' "$POD_NAME"
    printf 'pod_uid=%s\n' "$POD_UID"
} >> "$RESULT_DIR/summary.txt"

info "resource_control runtime target integration result saved to $RESULT_DIR"
