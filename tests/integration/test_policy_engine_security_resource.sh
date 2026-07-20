#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR"

ROOT="/sys/fs/cgroup/eulerpilot"
TARGET="$ROOT/policy-engine-background"
RESULT_DIR="${RESULT_DIR:-results/policy_engine/security-resource-$(date +%Y%m%d-%H%M%S)}"
AGENT_PID=""
TARGET_PID=""

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
    if [ -n "$TARGET_PID" ] && kill -0 "$TARGET_PID" 2>/dev/null; then
        kill "$TARGET_PID" 2>/dev/null
        wait "$TARGET_PID" 2>/dev/null
    fi
    scripts/rollback.sh > "$RESULT_DIR/rollback.log" 2>&1 || true
    rmdir "$TARGET" >/dev/null 2>&1 || true
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

[ "$(id -u)" -eq 0 ] || fail 'policy_engine integration test must run as root'

TRUE_BIN="$(command -v true || true)"
[ -n "$TRUE_BIN" ] || fail 'missing true binary for execve anomaly trigger'

mkdir -p "$RESULT_DIR" reports/events run/eulerpilot
make agent security-policy
scripts/setup_cgroup_v2.sh > "$RESULT_DIR/setup.log" 2>&1
: > reports/events/security_policy.jsonl
: > reports/events/policy_engine.jsonl
: > reports/events/resource_control.jsonl
: > run/eulerpilot/action_journal.jsonl

init_leaf_cgroup "$TARGET"
[ -w "$TARGET/cpu.max" ] || fail "$TARGET/cpu.max is not writable"
[ -w "$TARGET/memory.high" ] || fail "$TARGET/memory.high is not writable"

OLD_CPU_MAX="$(cat "$TARGET/cpu.max")"
OLD_MEMORY_HIGH="$(cat "$TARGET/memory.high")"

yes > /dev/null &
TARGET_PID="$!"
echo "$TARGET_PID" > "$TARGET/cgroup.procs"

cat > "$RESULT_DIR/agent.policy-engine.yaml" <<'YAML'
agent:
  name: EulerPilot
  mode: active
skills_config_path: skills.policy-engine.yaml
scheduler:
  type: cgroup_v2
exporter:
  prometheus:
    enabled: false
YAML

cat > "$RESULT_DIR/skills.policy-engine.yaml" <<YAML
schema_version: 2
skills:
- name: resource_control
  kind: runtime
  enabled: true
  config:
    mode: audit
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
- name: security_policy
  kind: runtime
  enabled: true
  config:
    mode: audit
    targets:
      demo_secret:
        type: path
        path: $ROOT/demo/security_policy_demo/secret.txt
        exec_path: $ROOT/demo/security_policy_demo/deny_exec.sh
    rules:
      - name: deny_demo_secret_open
        hook: lsm_file_open
        target_ref: demo_secret
        action: deny
      - name: deny_demo_exec
        hook: lsm_bprm_check_security
        target_ref: demo_secret
        action: deny
    anomaly_rules:
      - name: burst_execve
        type: rate
        syscall: execve
        threshold: 4
        window_ms: 2000
        severity: medium
- name: policy_engine
  kind: runtime
  enabled: true
  config:
    mode: enforce
    source:
      audit_path: reports/events/security_policy.jsonl
    watch:
      skill: security_policy
      operation: anomaly
      rule_id: burst_execve
      result: observed
    targets:
      anomaly_background:
        type: cgroup
        path: $TARGET
    actions:
      - name: throttle_anomaly_background_cpu
        target_ref: anomaly_background
        file: cpu.max
        value: '10000 100000'
      - name: cap_anomaly_background_memory
        target_ref: anomaly_background
        file: memory.high
        value: '1048576'
YAML

timeout 25s ./build/eulerpilot-agent \
    --config "$RESULT_DIR/agent.policy-engine.yaml" \
    --backend cgroup_v2 \
    --gate-mode normal \
    --active \
    --duration-s 10 \
    --interval-ms 500 \
    --jsonl \
    > "$RESULT_DIR/agent.log" 2>&1 &
AGENT_PID="$!"
sleep 1

if ! kill -0 "$AGENT_PID" 2>/dev/null; then
    set +e
    wait "$AGENT_PID"
    agent_rc="$?"
    set -e
    AGENT_PID=""
    fail "policy_engine agent exited early, rc=$agent_rc; see $RESULT_DIR/agent.log"
fi

for idx in $(seq 1 8); do
    "$TRUE_BIN" > "$RESULT_DIR/true-$idx.txt" 2> "$RESULT_DIR/true-$idx.err"
done

wait_for_value "$TARGET/cpu.max" '10000 100000' ||
    fail 'policy_engine did not apply anomaly cpu.max response'
wait_for_value "$TARGET/memory.high" '1048576' ||
    fail 'policy_engine did not apply anomaly memory.high response'

grep -q '"skill":"security_policy"' reports/events/security_policy.jsonl ||
    fail 'security_policy event log missing'
grep -q '"operation":"anomaly"' reports/events/security_policy.jsonl ||
    fail 'security anomaly event missing'
grep -q '"skill":"policy_engine"' reports/events/policy_engine.jsonl ||
    fail 'policy_engine event log missing'
grep -q '"operation":"cross_skill_response"' reports/events/policy_engine.jsonl ||
    fail 'policy_engine cross_skill_response event missing'
grep -q '"result":"applied"' reports/events/policy_engine.jsonl ||
    fail 'policy_engine applied result missing'
grep -q '"skill":"policy_engine"' run/eulerpilot/action_journal.jsonl ||
    fail 'policy_engine journal entry missing'

set +e
wait "$AGENT_PID"
agent_rc="$?"
set -e
AGENT_PID=""
[ "$agent_rc" -eq 0 ] || fail "policy_engine agent exited non-zero, rc=$agent_rc"

wait_for_value "$TARGET/cpu.max" "$OLD_CPU_MAX" ||
    fail 'policy_engine did not restore cpu.max after agent stop'
wait_for_value "$TARGET/memory.high" "$OLD_MEMORY_HIGH" ||
    fail 'policy_engine did not restore memory.high after agent stop'
grep -q '"result":"restored"' reports/events/policy_engine.jsonl ||
    fail 'policy_engine restored event missing'

cp reports/events/security_policy.jsonl "$RESULT_DIR/security_policy_events.jsonl"
cp reports/events/policy_engine.jsonl "$RESULT_DIR/policy_engine_events.jsonl"
cp run/eulerpilot/action_journal.jsonl "$RESULT_DIR/action_journal.jsonl"

cat > "$RESULT_DIR/summary.txt" <<EOF_SUMMARY
result=pass
linkage=security_anomaly_to_resource_degrade
source_skill=security_policy
source_rule=burst_execve
target_skill=policy_engine
target_cgroup=$TARGET
cpu_max_response=10000 100000
memory_high_response=1048576
old_cpu_max=$OLD_CPU_MAX
old_memory_high=$OLD_MEMORY_HIGH
target_pid=$TARGET_PID
EOF_SUMMARY

cat > "$RESULT_DIR/report.md" <<EOF_REPORT
# Policy Engine Security -> Resource Control Integration

- result: \`pass\`
- source: \`security_policy anomaly/burst_execve\`
- response: \`policy_engine cross_skill_response\`
- target cgroup: \`$TARGET\`
- cpu.max response: \`10000 100000\`
- memory.high response: \`1048576\`

The test verifies that a Security anomaly event can trigger a Resource Control downgrade action through the unified Agent process. It also verifies ActionJournal evidence and rollback to the old cgroup values after Agent exit.
EOF_REPORT

info "policy_engine integration result saved to $RESULT_DIR"
