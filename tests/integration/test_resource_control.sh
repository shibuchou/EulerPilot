#!/usr/bin/env bash
set -euo pipefail

ROOT="/sys/fs/cgroup/eulerpilot"
BG="$ROOT/background"
RESULT_DIR="${RESULT_DIR:-results/resource_control/integration-$(date +%Y%m%d-%H%M%S)}"
AGENT_PID=""
HOG_PID=""

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
    if [ -n "$HOG_PID" ] && kill -0 "$HOG_PID" 2>/dev/null; then
        kill "$HOG_PID" 2>/dev/null
        wait "$HOG_PID" 2>/dev/null
    fi
    scripts/rollback.sh > "$RESULT_DIR/rollback.log" 2>&1
}

wait_for_value() {
    local file="$1"
    local expected="$2"
    local deadline=$((SECONDS + 15))
    while [ "$SECONDS" -lt "$deadline" ]; do
        if [ -f "$file" ] && [ "$(cat "$file")" = "$expected" ]; then
            return 0
        fi
        sleep 0.2
    done
    printf '[DEBUG] %s now=%s expected=%s\n' "$file" "$(cat "$file" 2>/dev/null || true)" "$expected" >&2
    return 1
}

memory_high_events() {
    awk '/^high / { print $2 }' "$BG/memory.events" 2>/dev/null || printf '0\n'
}

[ "$(id -u)" -eq 0 ] || fail 'resource_control integration test must run as root'

mkdir -p "$RESULT_DIR"
trap cleanup EXIT

make agent
scripts/setup_cgroup_v2.sh > "$RESULT_DIR/setup.log" 2>&1
mkdir -p reports/events run/eulerpilot
: > reports/events/resource_control.jsonl
: > run/eulerpilot/action_journal.jsonl

[ -w "$BG/cpu.max" ] || fail "$BG/cpu.max is not writable"
[ -w "$BG/memory.high" ] || fail "$BG/memory.high is not writable"
[ -f "$BG/memory.events" ] || fail "$BG/memory.events is missing"

OLD_CPU_MAX="$(cat "$BG/cpu.max")"
OLD_MEMORY_HIGH="$(cat "$BG/memory.high")"

yes > /dev/null &
HOG_PID="$!"
info "background hog pid=$HOG_PID"

cat > "$RESULT_DIR/agent.resource-control.yaml" <<'YAML'
agent:
  name: EulerPilot
  mode: active
skills_config_path: skills.resource-control.yaml
scheduler:
  type: cgroup_v2
exporter:
  prometheus:
    enabled: false
YAML

cat > "$RESULT_DIR/skills.resource-control.yaml" <<'YAML'
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
    profiles:
      latency:
        cpu_max: max
        memory_low: '67108864'
        memory_high: max
        memory_max: max
      background:
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

timeout 25s ./build/eulerpilot-agent \
    --config "$RESULT_DIR/agent.resource-control.yaml" \
    --backend cgroup_v2 \
    --gate-mode always-active \
    --active \
    --duration-s 12 \
    --interval-ms 500 \
    --jsonl \
    > "$RESULT_DIR/agent.log" 2>&1 &
AGENT_PID="$!"

wait_for_value "$BG/cpu.max" '10000 100000' || fail 'cpu.max pressure value was not applied'
wait_for_value "$BG/memory.high" '1048576' || fail 'memory.high pressure value was not applied'

if ! grep -qw "$HOG_PID" "$BG/cgroup.procs"; then
    fail 'background workload was not moved into the EulerPilot background cgroup'
fi

BEFORE_HIGH="$(memory_high_events)"
BG_CGROUP="$BG" python3 - <<'PY'
import os
import time

bg = os.environ["BG_CGROUP"]
with open(os.path.join(bg, "cgroup.procs"), "w", encoding="utf-8") as procs:
    procs.write(str(os.getpid()))

chunks = []
for _ in range(64):
    chunks.append(bytearray(1024 * 1024))
    time.sleep(0.01)
time.sleep(0.2)
PY
AFTER_HIGH="$(memory_high_events)"

if [ "$AFTER_HIGH" -le "$BEFORE_HIGH" ]; then
    fail "memory.high event counter did not increase: before=$BEFORE_HIGH after=$AFTER_HIGH"
fi

wait "$AGENT_PID"
AGENT_PID=""

wait_for_value "$BG/cpu.max" "$OLD_CPU_MAX" || fail 'cpu.max was not restored after agent stop'
wait_for_value "$BG/memory.high" "$OLD_MEMORY_HIGH" || fail 'memory.high was not restored after agent stop'

grep -q '"file":"cpu.max"' reports/events/resource_control.jsonl || fail 'resource_control audit log missing cpu.max event'
grep -q '"file":"memory.high"' reports/events/resource_control.jsonl || fail 'resource_control audit log missing memory.high event'
grep -q '"result":"applied"' reports/events/resource_control.jsonl || fail 'resource_control audit log missing applied result'

cp reports/events/resource_control.jsonl "$RESULT_DIR/resource_control_events.jsonl"
cat > "$RESULT_DIR/summary.txt" <<EOF_SUMMARY
result=pass
hog_pid=$HOG_PID
cpu_max_pressure=10000 100000
memory_high_pressure=1048576
memory_high_events_before=$BEFORE_HIGH
memory_high_events_after=$AFTER_HIGH
old_cpu_max=$OLD_CPU_MAX
old_memory_high=$OLD_MEMORY_HIGH
EOF_SUMMARY

info "resource_control integration result saved to $RESULT_DIR"
