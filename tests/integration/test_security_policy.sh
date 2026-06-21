#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd -P)"
EXPECTED_ROOT="/root/EulerPilot"
RESULT_DIR="$ROOT/results/security_policy/integration-$(date +%Y%m%d-%H%M%S)"
AGENT_BIN="$ROOT/build/eulerpilot-agent"
TARGET_FILE="$ROOT/demo/security_policy_demo/secret.txt"
AGENT_PID=""
RESULT_READY="false"

log() {
    if [ "$RESULT_READY" = "true" ]; then
        echo "$*" | tee -a "$RESULT_DIR/test.log"
    else
        echo "$*"
    fi
}

fail() {
    log "FAIL: $*"
    exit 1
}

skip() {
    log "SKIP: $*"
    exit 77
}

require_cmd() {
    local cmd="$1"
    if ! command -v "$cmd" >/dev/null 2>&1; then
        skip "missing command: $cmd"
    fi
}

run_cleanup_script() {
    if [ -f "$ROOT/scripts/cleanup_security_policy_demo.sh" ]; then
        bash "$ROOT/scripts/cleanup_security_policy_demo.sh" >> "$RESULT_DIR/cleanup.log" 2>&1 || \
            log "WARN: cleanup_security_policy_demo.sh returned non-zero; continuing with manual pin cleanup"
    fi
    rm -f /sys/fs/bpf/security_policy_demo /sys/fs/bpf/security_policy_demo_link 2>/dev/null || true
}

restore() {
    set +e
    if [ -n "${AGENT_PID:-}" ] && kill -0 "$AGENT_PID" 2>/dev/null; then
        kill "$AGENT_PID" 2>/dev/null || true
        wait "$AGENT_PID" 2>/dev/null || true
    fi
    run_cleanup_script
    set -e
}

log "=== SecurityPolicyDemoSkill integration test ==="

if [ "$ROOT" != "$EXPECTED_ROOT" ]; then
    skip "current root is $ROOT; current demo hardcodes $EXPECTED_ROOT in Agent and BPF path checks"
fi

if [ "$(id -u)" -ne 0 ]; then
    skip "BPF LSM attach requires root or equivalent capabilities"
fi

require_cmd make
require_cmd bpftool
require_cmd timeout
require_cmd sed
require_cmd grep

if [ ! -r /sys/kernel/security/lsm ]; then
    skip "/sys/kernel/security/lsm is not readable; securityfs or LSM support may be missing"
fi
if ! grep -qw bpf /sys/kernel/security/lsm; then
    skip "BPF LSM is not enabled; /sys/kernel/security/lsm=$(cat /sys/kernel/security/lsm 2>/dev/null || true)"
fi
if [ ! -d /sys/fs/bpf ]; then
    skip "bpffs is not mounted at /sys/fs/bpf"
fi

mkdir -p "$RESULT_DIR"
RESULT_READY="true"
trap restore EXIT
log "INFO: result dir: $RESULT_DIR"

if [ ! -f "$TARGET_FILE" ]; then
    fail "target file missing: $TARGET_FILE"
fi

run_cleanup_script
rm -f "$ROOT/reports/events/security_policy.jsonl"

log "INFO: building agent and security-policy-demo BPF object"
if ! make agent security-policy-demo > "$RESULT_DIR/build.log" 2>&1; then
    fail "build failed; see $RESULT_DIR/build.log"
fi
log "PASS: build succeeds"

if [ ! -x "$AGENT_BIN" ]; then
    fail "agent binary missing after build: $AGENT_BIN"
fi
if [ ! -f "$ROOT/build/security_policy_demo.bpf.o" ]; then
    fail "BPF object missing after build: $ROOT/build/security_policy_demo.bpf.o"
fi

SKILLS_OUT="$("$AGENT_BIN" --list-skills | sed 's/\x1b\[[0-9;]*m//g')"
if ! echo "$SKILLS_OUT" | grep -qx "security_policy"; then
    fail "security_policy skill is not registered"
fi
if ! echo "$SKILLS_OUT" | grep -qx "security_policy_demo"; then
    fail "security_policy_demo compatibility skill is not registered"
fi
log "PASS: security_policy and compatibility demo skills are registered"

cat > "$RESULT_DIR/agent.audit.yaml" <<'YAML'
skills_config_path: skills.audit.yaml
exporter:
  prometheus:
    enabled: false
YAML

cat > "$RESULT_DIR/skills.audit.yaml" <<'YAML'
schema_version: 2
skills:
- name: resource_control
  kind: runtime
  enabled: true
  config: {}
- name: psi_gate
  kind: runtime
  enabled: true
  config: {}
- name: security_policy
  kind: runtime
  enabled: true
  config:
    mode: audit
    targets:
      demo_secret:
        type: path
        path: /root/EulerPilot/demo/security_policy_demo/secret.txt
    rules:
      - name: deny_demo_secret_open
        hook: lsm_file_open
        target_ref: demo_secret
        action: deny
YAML

if ! cat "$TARGET_FILE" > "$RESULT_DIR/baseline-secret.txt" 2> "$RESULT_DIR/baseline-secret.err"; then
    fail "target file is not readable before policy attach; see $RESULT_DIR/baseline-secret.err"
fi
log "PASS: target file is readable before policy attach"

rm -f "$ROOT/reports/events/security_policy.jsonl"

timeout 12s "$AGENT_BIN" \
    --config "$RESULT_DIR/agent.audit.yaml" \
    --duration-s 4 \
    --interval-ms 1000 \
    --jsonl \
    > "$RESULT_DIR/agent-audit.log" 2>&1 &
AGENT_PID="$!"
sleep 1

if ! kill -0 "$AGENT_PID" 2>/dev/null; then
    set +e
    wait "$AGENT_PID"
    agent_rc="$?"
    set -e
    AGENT_PID=""
    fail "audit agent exited early, rc=$agent_rc; see $RESULT_DIR/agent-audit.log"
fi

if ! cat "$TARGET_FILE" > "$RESULT_DIR/audit-secret.txt" 2> "$RESULT_DIR/audit-secret.err"; then
    fail "audit mode blocked target file; see $RESULT_DIR/audit-secret.err"
fi

set +e
wait "$AGENT_PID"
agent_rc="$?"
set -e
AGENT_PID=""
if [ "$agent_rc" -ne 0 ]; then
    fail "audit agent exited non-zero, rc=$agent_rc; see $RESULT_DIR/agent-audit.log"
fi
if ! grep -q '"skill":"security_policy"' "$ROOT/reports/events/security_policy.jsonl" 2>/dev/null; then
    fail "audit mode did not write security_policy audit event"
fi
if ! grep -q '"operation":"hit"' "$ROOT/reports/events/security_policy.jsonl" 2>/dev/null; then
    fail "audit mode did not write BPF hit event"
fi
if ! grep -q '"event_hook":"lsm_file_open"' "$ROOT/reports/events/security_policy.jsonl" 2>/dev/null; then
    fail "audit mode did not write lsm_file_open hit event"
fi
if ! grep -q '"event_hook":"sys_enter_execve"' "$ROOT/reports/events/security_policy.jsonl" 2>/dev/null; then
    fail "audit mode did not write execve trace event"
fi
if ! grep -q '"event_hook":"sys_enter_openat"' "$ROOT/reports/events/security_policy.jsonl" 2>/dev/null; then
    fail "audit mode did not write openat trace event"
fi
if ! grep -q '"result":"observed"' "$ROOT/reports/events/security_policy.jsonl" 2>/dev/null; then
    fail "audit mode hit event was not observed/allowed"
fi
cp "$ROOT/reports/events/security_policy.jsonl" "$RESULT_DIR/security_policy_events.audit.jsonl"
log "PASS: security_policy audit mode writes LSM, execve and openat hit events"

cat > "$RESULT_DIR/agent.enforce.yaml" <<'YAML'
skills_config_path: skills.enforce.yaml
exporter:
  prometheus:
    enabled: false
YAML

cat > "$RESULT_DIR/skills.enforce.yaml" <<'YAML'
schema_version: 2
skills:
- name: resource_control
  kind: runtime
  enabled: true
  config: {}
- name: psi_gate
  kind: runtime
  enabled: true
  config: {}
- name: security_policy
  kind: runtime
  enabled: true
  config:
    mode: enforce
    targets:
      demo_secret:
        type: path
        path: /root/EulerPilot/demo/security_policy_demo/secret.txt
    rules:
      - name: deny_demo_secret_open
        hook: lsm_file_open
        target_ref: demo_secret
        action: deny
YAML

rm -f "$ROOT/reports/events/security_policy.jsonl"

timeout 20s "$AGENT_BIN" \
    --config "$RESULT_DIR/agent.enforce.yaml" \
    --duration-s 8 \
    --interval-ms 1000 \
    --jsonl \
    > "$RESULT_DIR/agent-enforce.log" 2>&1 &
AGENT_PID="$!"

blocked="false"
for _ in $(seq 1 40); do
    if ! kill -0 "$AGENT_PID" 2>/dev/null; then
        set +e
        wait "$AGENT_PID"
        agent_rc="$?"
        set -e
        AGENT_PID=""
        fail "agent exited before policy denial was observed, rc=$agent_rc; see $RESULT_DIR/agent-enforce.log"
    fi

    set +e
    cat "$TARGET_FILE" > "$RESULT_DIR/blocked-secret.txt" 2> "$RESULT_DIR/blocked-secret.err"
    cat_rc="$?"
    set -e
    if [ "$cat_rc" -ne 0 ]; then
        blocked="true"
        break
    fi
    sleep 0.2
done

if [ "$blocked" != "true" ]; then
    fail "target file was not denied while agent was running; see $RESULT_DIR/agent-enforce.log"
fi
log "PASS: target file is denied while security_policy enforce is active"

if ! grep -Eqi "Operation not permitted|Permission denied|权限|不允许" "$RESULT_DIR/blocked-secret.err"; then
    log "WARN: denial stderr did not contain the usual permission text; see $RESULT_DIR/blocked-secret.err"
fi

set +e
wait "$AGENT_PID"
agent_rc="$?"
set -e
AGENT_PID=""
if [ "$agent_rc" -ne 0 ]; then
    fail "agent exited non-zero after policy test, rc=$agent_rc; see $RESULT_DIR/agent-enforce.log"
fi
log "PASS: agent exits cleanly"

if ! grep -q '"operation":"hit"' "$ROOT/reports/events/security_policy.jsonl" 2>/dev/null; then
    fail "enforce mode did not write BPF hit event"
fi
if ! grep -q '"event_hook":"lsm_file_open"' "$ROOT/reports/events/security_policy.jsonl" 2>/dev/null; then
    fail "enforce mode did not write lsm_file_open hit event"
fi
if ! grep -q '"result":"blocked"' "$ROOT/reports/events/security_policy.jsonl" 2>/dev/null; then
    fail "enforce mode hit event was not blocked"
fi
cp "$ROOT/reports/events/security_policy.jsonl" "$RESULT_DIR/security_policy_events.enforce.jsonl"
log "PASS: security_policy enforce mode writes blocked BPF hit event"

run_cleanup_script

if ! cat "$TARGET_FILE" > "$RESULT_DIR/post-cleanup-secret.txt" 2> "$RESULT_DIR/post-cleanup-secret.err"; then
    fail "target file is still denied after rollback/cleanup; see $RESULT_DIR/post-cleanup-secret.err"
fi
log "PASS: target file is readable after agent rollback"

if bpftool link show 2>/dev/null | grep -q "security_policy_demo"; then
    bpftool link show > "$RESULT_DIR/bpftool-link-after-cleanup.txt" 2>&1 || true
    fail "security_policy_demo BPF link residue found; see $RESULT_DIR/bpftool-link-after-cleanup.txt"
fi
if [ -e /sys/fs/bpf/security_policy_demo ] || [ -e /sys/fs/bpf/security_policy_demo_link ]; then
    fail "security_policy_demo pinned object residue found under /sys/fs/bpf"
fi
log "PASS: cleanup leaves no known security_policy_demo BPF residue"

log "=== SecurityPolicyDemoSkill integration test complete ==="
