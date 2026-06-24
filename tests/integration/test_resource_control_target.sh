#!/usr/bin/env bash
set -euo pipefail

ROOT="/sys/fs/cgroup/eulerpilot"
TARGET="$ROOT/target-background"
OUTSIDE="$ROOT/outside-background"
RESULT_DIR="${RESULT_DIR:-results/resource_control/target-$(date +%Y%m%d-%H%M%S)}"
AGENT_PID=""
TARGET_PID=""
OUTSIDE_PID=""

fail() {
    printf '[FAIL] %s\n' "$*" >&2
    exit 1
}

info() {
    printf '[INFO] %s\n' "$*"
}

cleanup() {
    set +e
    if [ -n "$AGENT_PID" ] && kill -0 "$AGENT_PID" 2>/dev/null; then
        kill "$AGENT_PID" 2>/dev/null
        wait "$AGENT_PID" 2>/dev/null
    fi
    for pid in "$TARGET_PID" "$OUTSIDE_PID"; do
        if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
            kill "$pid" 2>/dev/null
            wait "$pid" 2>/dev/null
        fi
    done
    scripts/rollback.sh > "$RESULT_DIR/rollback.log" 2>&1
    rmdir "$TARGET" "$OUTSIDE" >/dev/null 2>&1 || true
}

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
    printf '[DEBUG] %s now=%s expected=%s\n' "$file" "$(cat "$file" 2>/dev/null || true)" "$expected" >&2
    return 1
}

init_leaf_cgroup() {
    local path="$1"
    mkdir -p "$path"
    [ -w "$path/cpuset.mems" ] && echo 0 > "$path/cpuset.mems" || true
    [ -w "$path/cpuset.cpus" ] && echo 0-1 > "$path/cpuset.cpus" || true
    [ -w "$path/cpu.max" ] && echo max > "$path/cpu.max" || true
    [ -w "$path/memory.high" ] && echo max > "$path/memory.high" || true
    [ -w "$path/memory.low" ] && echo 0 > "$path/memory.low" || true
    [ -w "$path/memory.max" ] && echo max > "$path/memory.max" || true
}

[ "$(id -u)" -eq 0 ] || fail 'resource_control target integration test must run as root'

mkdir -p "$RESULT_DIR"
trap cleanup EXIT

make agent
scripts/setup_cgroup_v2.sh > "$RESULT_DIR/setup.log" 2>&1
mkdir -p reports/events run/eulerpilot
: > reports/events/resource_control.jsonl
: > run/eulerpilot/action_journal.jsonl

init_leaf_cgroup "$TARGET"
init_leaf_cgroup "$OUTSIDE"

[ -w "$TARGET/cpu.max" ] || fail "$TARGET/cpu.max is not writable"
[ -w "$TARGET/memory.high" ] || fail "$TARGET/memory.high is not writable"
[ -w "$OUTSIDE/cpu.max" ] || fail "$OUTSIDE/cpu.max is not writable"

OLD_TARGET_CPU_MAX="$(cat "$TARGET/cpu.max")"
OLD_TARGET_MEMORY_HIGH="$(cat "$TARGET/memory.high")"
OLD_OUTSIDE_CPU_MAX="$(cat "$OUTSIDE/cpu.max")"

yes > /dev/null &
TARGET_PID="$!"
echo "$TARGET_PID" > "$TARGET/cgroup.procs"

yes > /dev/null &
OUTSIDE_PID="$!"
echo "$OUTSIDE_PID" > "$OUTSIDE/cgroup.procs"

info "target background pid=$TARGET_PID outside pid=$OUTSIDE_PID"

cat > "$RESULT_DIR/agent.resource-control-target.yaml" <<'YAML'
agent:
  name: EulerPilot
  mode: active
skills_config_path: skills.resource-control-target.yaml
scheduler:
  type: cgroup_v2
exporter:
  prometheus:
    enabled: false
YAML

cat > "$RESULT_DIR/skills.resource-control-target.yaml" <<YAML
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
      background_scope:
        type: cgroup
        path: $TARGET
    profiles:
      background:
        target_ref: background_scope
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

timeout 30s ./build/eulerpilot-agent \
    --config "$RESULT_DIR/agent.resource-control-target.yaml" \
    --backend cgroup_v2 \
    --gate-mode always-active \
    --active \
    --duration-s 14 \
    --interval-ms 500 \
    --jsonl \
    > "$RESULT_DIR/agent.log" 2>&1 &
AGENT_PID="$!"

wait_for_value "$TARGET/cpu.max" '10000 100000' || fail 'target cpu.max pressure value was not applied'
wait_for_value "$TARGET/memory.high" '1048576' || fail 'target memory.high pressure value was not applied'

if [ "$(cat "$OUTSIDE/cpu.max")" != "$OLD_OUTSIDE_CPU_MAX" ]; then
    fail "outside cgroup cpu.max changed unexpectedly: $(cat "$OUTSIDE/cpu.max")"
fi

if ! grep -qw "$TARGET_PID" "$TARGET/cgroup.procs"; then
    fail 'target workload left the configured target cgroup unexpectedly'
fi
if ! grep -qw "$OUTSIDE_PID" "$OUTSIDE/cgroup.procs"; then
    fail 'outside workload moved unexpectedly'
fi

wait "$AGENT_PID"
AGENT_PID=""

wait_for_value "$TARGET/cpu.max" "$OLD_TARGET_CPU_MAX" || fail 'target cpu.max was not restored after agent stop'
wait_for_value "$TARGET/memory.high" "$OLD_TARGET_MEMORY_HIGH" || fail 'target memory.high was not restored after agent stop'

grep -q '"target_ref":"background_scope"' reports/events/resource_control.jsonl || fail 'audit log missing target_ref=background_scope'
grep -q "\"cgroup\":\"$TARGET\"" reports/events/resource_control.jsonl || fail 'audit log missing target cgroup path'
grep -q '"file":"cpu.max"' reports/events/resource_control.jsonl || fail 'audit log missing cpu.max event'
grep -q '"file":"memory.high"' reports/events/resource_control.jsonl || fail 'audit log missing memory.high event'
grep -q '"result":"applied"' reports/events/resource_control.jsonl || fail 'audit log missing applied result'
grep -q '"result":"restored"' reports/events/resource_control.jsonl || fail 'audit log missing restored result'
grep -q '"target_ref":"background_scope"' "$RESULT_DIR/agent.log" || fail 'agent jsonl missing target_ref'

cp reports/events/resource_control.jsonl "$RESULT_DIR/resource_control_events.jsonl"
cat > "$RESULT_DIR/summary.txt" <<EOF_SUMMARY
result=pass
target_ref=background_scope
target_cgroup=$TARGET
outside_cgroup=$OUTSIDE
target_pid=$TARGET_PID
outside_pid=$OUTSIDE_PID
cpu_max_pressure=10000 100000
memory_high_pressure=1048576
old_target_cpu_max=$OLD_TARGET_CPU_MAX
old_target_memory_high=$OLD_TARGET_MEMORY_HIGH
outside_cpu_max=$OLD_OUTSIDE_CPU_MAX
EOF_SUMMARY

info "resource_control target integration result saved to $RESULT_DIR"
