#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR"

RESULT_DIR="${RESULT_DIR:-results/resource_control/real-runtime-target-$(date +%Y%m%d-%H%M%S)}"
RUNTIME_KIND="${EULERPILOT_RUNTIME_KIND:-auto}"
RUNTIME_BIN="${EULERPILOT_RUNTIME_BIN:-}"
RUNTIME_IMAGE="${EULERPILOT_RUNTIME_IMAGE:-busybox:latest}"
ALLOW_IMAGE_PULL="${EULERPILOT_ALLOW_IMAGE_PULL:-0}"
CONTAINER_NAME="${EULERPILOT_RUNTIME_CONTAINER_NAME:-eulerpilot-rc-real-runtime}"

AGENT_PID=""
CONTAINER_STARTED=0
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
    local cmd="$1"
    command -v "$cmd" 2>/dev/null || true
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
        printf 'runtime_kind=%s\n' "$RUNTIME_KIND"
        printf 'runtime_bin=%s\n' "${RUNTIME_BIN:-missing}"
        printf 'runtime_image=%s\n' "$RUNTIME_IMAGE"
        printf 'container_name=%s\n' "$CONTAINER_NAME"
        printf 'docker_command=%s\n' "$(command_path docker || true)"
        printf 'podman_command=%s\n' "$(command_path podman || true)"
        printf 'isula_command=%s\n' "$(command_path isula || true)"
        printf 'next_action=%s\n' "$next_action"
    } > "$RESULT_DIR/summary.txt"
}

write_report() {
    local result reason
    result="$(awk -F= '$1=="result"{print $2}' "$RESULT_DIR/summary.txt" 2>/dev/null || true)"
    reason="$(awk -F= '$1=="reason"{print $2}' "$RESULT_DIR/summary.txt" 2>/dev/null || true)"
    cat > "$RESULT_DIR/report.md" <<EOF_REPORT
# Resource Control Real Runtime Target

- result: \`${result:-unknown}\`
- reason: \`${reason:-unknown}\`
- host: \`$(hostname 2>/dev/null || printf unknown)\`
- kernel: \`$(uname -r)\`
- runtime: \`${RUNTIME_KIND}\`
- image: \`${RUNTIME_IMAGE}\`

## Purpose

This test is the real-runtime companion of \`test_resource_control_runtime_target.sh\`. It runs a real docker, podman, or iSulad/isula container when a local runtime and image are available, configures \`resource_control.target_ref\` with \`type: container\`, and verifies that EulerPilot applies and restores \`cpu.max\` and \`memory.high\` on the resolved container cgroup.

When docker/podman/isula or the requested image is missing, the script exits with \`result=blocked\` and records the exact next action instead of silently installing packages or pulling images.

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
    if [ "$CONTAINER_STARTED" = "1" ] && [ -n "${RUNTIME_BIN:-}" ]; then
        "$RUNTIME_BIN" rm -f "$CONTAINER_NAME" >> "$RESULT_DIR/commands.log" 2>&1 || true
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

select_runtime() {
    if [ -n "$RUNTIME_BIN" ]; then
        [ -x "$RUNTIME_BIN" ] || return 1
        [ "$RUNTIME_KIND" != "auto" ] || RUNTIME_KIND="$(basename "$RUNTIME_BIN")"
        return 0
    fi

    if [ "$RUNTIME_KIND" = "docker" ] || [ "$RUNTIME_KIND" = "auto" ]; then
        RUNTIME_BIN="$(command_path docker)"
        if [ -n "$RUNTIME_BIN" ]; then
            RUNTIME_KIND="docker"
            return 0
        fi
    fi

    if [ "$RUNTIME_KIND" = "podman" ] || [ "$RUNTIME_KIND" = "auto" ]; then
        RUNTIME_BIN="$(command_path podman)"
        if [ -n "$RUNTIME_BIN" ]; then
            RUNTIME_KIND="podman"
            return 0
        fi
    fi

    if [ "$RUNTIME_KIND" = "isula" ] || [ "$RUNTIME_KIND" = "isulad" ] || [ "$RUNTIME_KIND" = "auto" ]; then
        RUNTIME_BIN="$(command_path isula)"
        if [ -n "$RUNTIME_BIN" ]; then
            RUNTIME_KIND="isula"
            return 0
        fi
    fi

    return 1
}

runtime_ready() {
    timeout 8s "$RUNTIME_BIN" ps >/dev/null 2>"$RESULT_DIR/runtime_ps.err"
}

image_available() {
    if [ "$RUNTIME_KIND" = "isula" ]; then
        "$RUNTIME_BIN" inspect "$RUNTIME_IMAGE" >/dev/null 2>"$RESULT_DIR/image_inspect.err"
    else
        "$RUNTIME_BIN" image inspect "$RUNTIME_IMAGE" >/dev/null 2>"$RESULT_DIR/image_inspect.err"
    fi
}

container_cgroup_for_pid() {
    local pid="$1"
    local rel
    rel="$(awk -F: '$1=="0" || $2=="" {print $3; exit}' "/proc/$pid/cgroup")"
    if [ -z "$rel" ]; then
        return 1
    fi
    if [ "$rel" = "/" ]; then
        printf '/sys/fs/cgroup'
    else
        printf '/sys/fs/cgroup%s' "$rel"
    fi
}

write_agent_config() {
    cat > "$RESULT_DIR/agent.yaml" <<YAML
agent:
  name: EulerPilot
  mode: active
skills_config_path: $RESULT_DIR/skills.yaml
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
      real_container:
        type: container
        container_name: $CONTAINER_NAME
        runtime: $RUNTIME_KIND
        cgroup_root: /sys/fs/cgroup
        docker_path: $RUNTIME_BIN
        podman_path: $RUNTIME_BIN
        isula_path: $RUNTIME_BIN
    profiles:
      background:
        target_ref: real_container
        normal:
          cpu_max: max
          memory_high: max
        pressure:
          target_ref: real_container
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

[ "$(id -u)" -eq 0 ] || fail 'real runtime target test must run as root'

if ! select_runtime; then
    write_blocked "missing-docker-podman-or-isula" \
        "install-or-start-docker-podman-or-isula-then-rerun-this-script"
    write_report
    info "result directory: $RESULT_DIR"
    exit 0
fi

if ! runtime_ready; then
    write_blocked "runtime-not-ready" \
        "start-${RUNTIME_KIND}-service-or-fix-runtime-permission"
    write_report
    info "result directory: $RESULT_DIR"
    exit 0
fi

if ! image_available; then
    if [ "$ALLOW_IMAGE_PULL" != "1" ]; then
        write_blocked "runtime-image-missing" \
            "load-local-image-or-set-EULERPILOT_ALLOW_IMAGE_PULL=1"
        write_report
        info "result directory: $RESULT_DIR"
        exit 0
    fi
    log_cmd "$RUNTIME_BIN" pull "$RUNTIME_IMAGE"
fi

if [ ! -x ./build/eulerpilot-agent ]; then
    log_cmd make agent
fi

: > reports/events/resource_control.jsonl
: > run/eulerpilot/action_journal.jsonl

"$RUNTIME_BIN" rm -f "$CONTAINER_NAME" >> "$RESULT_DIR/commands.log" 2>&1 || true
log_cmd "$RUNTIME_BIN" run -d --name "$CONTAINER_NAME" "$RUNTIME_IMAGE" sh -c 'yes >/dev/null'
CONTAINER_STARTED=1

CONTAINER_ID="$("$RUNTIME_BIN" inspect -f '{{.Id}}' "$CONTAINER_NAME" | tr -d '[:space:]')"
CONTAINER_PID="$("$RUNTIME_BIN" inspect -f '{{.State.Pid}}' "$CONTAINER_NAME" | tr -d '[:space:]')"
if [ -z "$CONTAINER_ID" ] || [ -z "$CONTAINER_PID" ] || [ "$CONTAINER_PID" = "0" ]; then
    fail "failed to inspect real runtime container id or pid"
fi

TARGET_CGROUP="$(container_cgroup_for_pid "$CONTAINER_PID")"
if [ -z "$TARGET_CGROUP" ] || [ ! -d "$TARGET_CGROUP" ]; then
    write_blocked "container-cgroup-not-found-from-pid" \
        "check-runtime-cgroup-v2-configuration"
    write_report
    info "result directory: $RESULT_DIR"
    exit 0
fi

if ! find /sys/fs/cgroup -maxdepth 8 -type d -path "*$CONTAINER_ID*" \
    > "$RESULT_DIR/container_id_cgroup_scan.txt" 2>/dev/null ||
   [ ! -s "$RESULT_DIR/container_id_cgroup_scan.txt" ]; then
    write_blocked "container-id-cgroup-not-discoverable" \
        "verify-runtime-uses-container-id-in-cgroup-path-or-extend-target-resolver"
    write_kv "container_id" "$CONTAINER_ID"
    write_kv "container_pid" "$CONTAINER_PID"
    write_kv "pid_cgroup" "$TARGET_CGROUP"
    write_report
    info "result directory: $RESULT_DIR"
    exit 0
fi

if [ ! -w "$TARGET_CGROUP/cpu.max" ] || [ ! -w "$TARGET_CGROUP/memory.high" ]; then
    write_blocked "container-cgroup-not-writable" \
        "enable-cgroup-v2-cpu-memory-controllers-for-runtime-slice"
    write_kv "container_id" "$CONTAINER_ID"
    write_kv "container_pid" "$CONTAINER_PID"
    write_kv "target_cgroup" "$TARGET_CGROUP"
    write_report
    info "result directory: $RESULT_DIR"
    exit 0
fi

OLD_CPU_MAX="$(cat "$TARGET_CGROUP/cpu.max")"
OLD_MEMORY_HIGH="$(cat "$TARGET_CGROUP/memory.high")"
write_agent_config

timeout 35s ./build/eulerpilot-agent \
    --config "$RESULT_DIR/agent.yaml" \
    --backend cgroup_v2 \
    --gate-mode always-active \
    --active \
    --duration-s 12 \
    --interval-ms 500 \
    --jsonl \
    > "$RESULT_DIR/agent.log" 2>&1 &
AGENT_PID="$!"

wait_for_value "$TARGET_CGROUP/cpu.max" '10000 100000' ||
    fail "real container cpu.max pressure value was not applied"
wait_for_value "$TARGET_CGROUP/memory.high" '1048576' ||
    fail "real container memory.high pressure value was not applied"

wait "$AGENT_PID"
AGENT_PID=""

wait_for_value "$TARGET_CGROUP/cpu.max" "$OLD_CPU_MAX" ||
    fail "real container cpu.max was not restored"
wait_for_value "$TARGET_CGROUP/memory.high" "$OLD_MEMORY_HIGH" ||
    fail "real container memory.high was not restored"

cp reports/events/resource_control.jsonl "$RESULT_DIR/resource_control.jsonl"
grep -q '"target_ref":"real_container"' "$RESULT_DIR/resource_control.jsonl" ||
    fail "resource_control audit log missing real_container target_ref"
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
    printf 'reason=real-runtime-target-applied-and-restored\n'
    printf 'host=%s\n' "$(hostname 2>/dev/null || printf unknown)"
    printf 'date=%s\n' "$(date -Is)"
    printf 'kernel=%s\n' "$(uname -r)"
    printf 'runtime_kind=%s\n' "$RUNTIME_KIND"
    printf 'runtime_bin=%s\n' "$RUNTIME_BIN"
    printf 'runtime_image=%s\n' "$RUNTIME_IMAGE"
    printf 'container_name=%s\n' "$CONTAINER_NAME"
    printf 'container_id=%s\n' "$CONTAINER_ID"
    printf 'container_pid=%s\n' "$CONTAINER_PID"
    printf 'target_ref=real_container\n'
    printf 'target_cgroup=%s\n' "$TARGET_CGROUP"
    printf 'cpu_max_pressure=10000 100000\n'
    printf 'memory_high_pressure=1048576\n'
    printf 'old_cpu_max=%s\n' "$OLD_CPU_MAX"
    printf 'old_memory_high=%s\n' "$OLD_MEMORY_HIGH"
} > "$RESULT_DIR/summary.txt"

write_report
info "result directory: $RESULT_DIR"
