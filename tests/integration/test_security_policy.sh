#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd -P)"
EXPECTED_ROOT="/root/EulerPilot"
RESULT_DIR="$ROOT/results/security_policy/integration-$(date +%Y%m%d-%H%M%S)"
AGENT_BIN="$ROOT/build/eulerpilot-agent"
TARGET_FILE="$ROOT/demo/security_policy_demo/secret.txt"
EXEC_TARGET_FILE="$ROOT/demo/security_policy_demo/deny_exec.sh"
AGENT_PID=""
RESULT_READY="false"
DYNAMIC_DIR=""
DYNAMIC_TARGET_FILE=""
DYNAMIC_EXEC_TARGET_FILE=""
DYNAMIC_SECOND_TARGET_FILE=""
DYNAMIC_SECOND_EXEC_TARGET_FILE=""
DYNAMIC_SCOPED_TARGET_FILE=""
DYNAMIC_SCOPED_EXEC_TARGET_FILE=""
DYNAMIC_PREFIX_EXEC_FILE=""
DYNAMIC_WRITE_TARGET_FILE=""
SCOPED_CGROUP_PATH=""
SCOPED_PID=""
SOCKET_SERVER_PID=""
TRUE_BIN="/usr/bin/true"
if [ ! -x "$TRUE_BIN" ]; then
    TRUE_BIN="/bin/true"
fi

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

cleanup_scoped_cgroup() {
    if [ -n "${SCOPED_CGROUP_PATH:-}" ] && [ -d "$SCOPED_CGROUP_PATH" ]; then
        if [ -f "$SCOPED_CGROUP_PATH/cgroup.procs" ]; then
            while read -r pid; do
                if [ -n "$pid" ]; then
                    echo "$pid" > /sys/fs/cgroup/cgroup.procs 2>/dev/null || true
                fi
            done < "$SCOPED_CGROUP_PATH/cgroup.procs"
        fi
        rmdir "$SCOPED_CGROUP_PATH" 2>/dev/null || true
    fi
}

cleanup_scoped_pid() {
    if [ -n "${SCOPED_PID:-}" ]; then
        kill "$SCOPED_PID" 2>/dev/null || true
        wait "$SCOPED_PID" 2>/dev/null || true
        SCOPED_PID=""
    fi
}

cleanup_socket_server() {
    if [ -n "${SOCKET_SERVER_PID:-}" ]; then
        kill "$SOCKET_SERVER_PID" 2>/dev/null || true
        wait "$SOCKET_SERVER_PID" 2>/dev/null || true
        SOCKET_SERVER_PID=""
    fi
}

restore() {
    set +e
    if [ -n "${AGENT_PID:-}" ] && kill -0 "$AGENT_PID" 2>/dev/null; then
        kill "$AGENT_PID" 2>/dev/null || true
        wait "$AGENT_PID" 2>/dev/null || true
    fi
    run_cleanup_script
    cleanup_socket_server
    cleanup_scoped_pid
    cleanup_scoped_cgroup
    if [ -n "${DYNAMIC_DIR:-}" ] && [ -d "$DYNAMIC_DIR" ]; then
        rm -rf "$DYNAMIC_DIR"
    fi
    set -e
}

log "=== SecurityPolicyDemoSkill integration test ==="

if [ "$ROOT" != "$EXPECTED_ROOT" ]; then
    skip "current root is $ROOT; Agent BPF object path and demo result layout currently expect $EXPECTED_ROOT"
fi

if [ "$(id -u)" -ne 0 ]; then
    skip "BPF LSM attach requires root or equivalent capabilities"
fi

require_cmd make
require_cmd bpftool
require_cmd timeout
require_cmd sed
require_cmd grep
require_cmd python3
require_cmd unshare

trigger_audit_syscalls() {
    python3 - <<'PY'
import ctypes
import socket

try:
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(0.1)
    try:
        sock.connect(("127.0.0.1", 9))
    except OSError:
        pass
    sock.close()
except Exception:
    pass

try:
    libc = ctypes.CDLL(None, use_errno=True)
    # PTRACE_TRACEME is enough to generate sys_enter_ptrace; the return
    # value is not part of this audit-only validation.
    libc.ptrace(0, 0, None, None)
except Exception:
    pass
PY
}

assert_blocked_rule_event() {
    local path="$1"
    local hook="$2"
    local rule_id="$3"
    local target_ref="$4"

    if ! grep -F "$path" "$ROOT/reports/events/security_policy.jsonl" 2>/dev/null \
        | grep -F "\"event_hook\":\"$hook\"" \
        | grep -F '"result":"blocked"' \
        | grep -F "\"rule_id\":\"$rule_id\"" \
        | grep -Fq "\"target_ref\":\"$target_ref\""; then
        fail "blocked event for $path did not carry rule_id=$rule_id target_ref=$target_ref hook=$hook"
    fi
}

assert_blocked_rule_event_has_cgroup() {
    local path="$1"
    local hook="$2"
    local rule_id="$3"
    local target_ref="$4"

    if ! grep -F "$path" "$ROOT/reports/events/security_policy.jsonl" 2>/dev/null \
        | grep -F "\"event_hook\":\"$hook\"" \
        | grep -F '"result":"blocked"' \
        | grep -F "\"rule_id\":\"$rule_id\"" \
        | grep -F "\"target_ref\":\"$target_ref\"" \
        | grep -Fq '"cgroup_id":"'; then
        fail "blocked event for $path did not carry cgroup scoped rule metadata"
    fi
}

audit_hook_seen() {
    local hook="$1"
    grep -q "\"event_hook\":\"$hook\"" "$ROOT/reports/events/security_policy.jsonl" 2>/dev/null
}

audit_required_hooks_seen() {
    audit_hook_seen "lsm_file_open" &&
        audit_hook_seen "sys_enter_execve" &&
        audit_hook_seen "sys_enter_openat" &&
        audit_hook_seen "sys_enter_connect" &&
        audit_hook_seen "sys_enter_ptrace" &&
        audit_hook_seen "lsm_bprm_check_security"
}

run_in_scoped_cgroup() {
    local stdout_path="$1"
    local stderr_path="$2"
    shift 2
    bash -c 'echo $$ > "$1/cgroup.procs"; shift; exec "$@"' \
        _ "$SCOPED_CGROUP_PATH" "$@" > "$stdout_path" 2> "$stderr_path"
}

start_socket_server() {
    local port="$1"

    cat > "$RESULT_DIR/socket_server.py" <<'PY'
import socket
import sys
import time

port = int(sys.argv[1])
server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
server.bind(("127.0.0.1", port))
server.listen(32)
server.settimeout(0.5)
deadline = time.time() + 90
while time.time() < deadline:
    try:
        conn, _ = server.accept()
        conn.close()
    except socket.timeout:
        pass
server.close()
PY

    cat > "$RESULT_DIR/connect_ipv4.py" <<'PY'
import socket
import sys

port = int(sys.argv[1])
client = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
client.settimeout(1.0)
client.connect(("127.0.0.1", port))
client.close()
PY

    python3 "$RESULT_DIR/socket_server.py" "$port" > "$RESULT_DIR/socket-server.log" 2>&1 &
    SOCKET_SERVER_PID="$!"

    local ready="false"
    for _ in $(seq 1 50); do
        if python3 "$RESULT_DIR/connect_ipv4.py" "$port" >/dev/null 2>&1; then
            ready="true"
            break
        fi
        sleep 0.1
    done
    if [ "$ready" != "true" ]; then
        fail "socket test server did not become ready; see $RESULT_DIR/socket-server.log"
    fi
}

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
if [ ! -x "$EXEC_TARGET_FILE" ]; then
    fail "exec target missing or not executable: $EXEC_TARGET_FILE"
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
        exec_path: /root/EulerPilot/demo/security_policy_demo/deny_exec.sh
    rules:
      - name: deny_demo_secret_open
        hook: lsm_file_open
        target_ref: demo_secret
        action: deny
      - name: deny_demo_exec
        hook: lsm_bprm_check_security
        target_ref: demo_secret
        action: deny
YAML

if ! cat "$TARGET_FILE" > "$RESULT_DIR/baseline-secret.txt" 2> "$RESULT_DIR/baseline-secret.err"; then
    fail "target file is not readable before policy attach; see $RESULT_DIR/baseline-secret.err"
fi
log "PASS: target file is readable before policy attach"

if ! "$EXEC_TARGET_FILE" > "$RESULT_DIR/baseline-exec.txt" 2> "$RESULT_DIR/baseline-exec.err"; then
    fail "exec target is not runnable before policy attach; see $RESULT_DIR/baseline-exec.err"
fi
log "PASS: exec target is runnable before policy attach"

rm -f "$ROOT/reports/events/security_policy.jsonl"

timeout 18s "$AGENT_BIN" \
    --config "$RESULT_DIR/agent.audit.yaml" \
    --duration-s 8 \
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

audit_ready="false"
for _ in $(seq 1 30); do
    if ! kill -0 "$AGENT_PID" 2>/dev/null; then
        set +e
        wait "$AGENT_PID"
        agent_rc="$?"
        set -e
        AGENT_PID=""
        fail "audit agent exited before required events were observed, rc=$agent_rc; see $RESULT_DIR/agent-audit.log"
    fi

    if ! cat "$TARGET_FILE" > "$RESULT_DIR/audit-secret.txt" 2> "$RESULT_DIR/audit-secret.err"; then
        fail "audit mode blocked target file; see $RESULT_DIR/audit-secret.err"
    fi
    if ! "$EXEC_TARGET_FILE" > "$RESULT_DIR/audit-exec.txt" 2> "$RESULT_DIR/audit-exec.err"; then
        fail "audit mode blocked exec target; see $RESULT_DIR/audit-exec.err"
    fi
    trigger_audit_syscalls

    if audit_required_hooks_seen; then
        audit_ready="true"
        break
    fi
    sleep 0.1
done

if [ "$audit_ready" != "true" ]; then
    fail "audit mode did not observe all required LSM and syscall hooks before agent timeout"
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
if ! grep -q '"event_hook":"sys_enter_connect"' "$ROOT/reports/events/security_policy.jsonl" 2>/dev/null; then
    fail "audit mode did not write connect trace event"
fi
if ! grep -q '"event_hook":"sys_enter_ptrace"' "$ROOT/reports/events/security_policy.jsonl" 2>/dev/null; then
    fail "audit mode did not write ptrace trace event"
fi
if ! grep -q '"event_hook":"lsm_bprm_check_security"' "$ROOT/reports/events/security_policy.jsonl" 2>/dev/null; then
    fail "audit mode did not write bprm_check_security hit event"
fi
if ! grep -q '"result":"observed"' "$ROOT/reports/events/security_policy.jsonl" 2>/dev/null; then
    fail "audit mode hit event was not observed/allowed"
fi
cp "$ROOT/reports/events/security_policy.jsonl" "$RESULT_DIR/security_policy_events.audit.jsonl"
log "PASS: security_policy audit mode writes file, bprm and four syscall hit events"

if [ ! -x "$TRUE_BIN" ]; then
    skip "missing executable true binary for execve anomaly test"
fi

cat > "$RESULT_DIR/agent.anomaly.yaml" <<'YAML'
skills_config_path: skills.anomaly.yaml
exporter:
  prometheus:
    enabled: false
YAML

cat > "$RESULT_DIR/skills.anomaly.yaml" <<'YAML'
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
        exec_path: /root/EulerPilot/demo/security_policy_demo/deny_exec.sh
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
YAML

rm -f "$ROOT/reports/events/security_policy.jsonl"

timeout 18s "$AGENT_BIN" \
    --config "$RESULT_DIR/agent.anomaly.yaml" \
    --duration-s 8 \
    --interval-ms 1000 \
    --jsonl \
    > "$RESULT_DIR/agent-anomaly.log" 2>&1 &
AGENT_PID="$!"
sleep 1

if ! kill -0 "$AGENT_PID" 2>/dev/null; then
    set +e
    wait "$AGENT_PID"
    agent_rc="$?"
    set -e
    AGENT_PID=""
    fail "anomaly agent exited early, rc=$agent_rc; see $RESULT_DIR/agent-anomaly.log"
fi

for idx in $(seq 1 8); do
    if ! "$TRUE_BIN" > "$RESULT_DIR/anomaly-true-$idx.txt" \
        2> "$RESULT_DIR/anomaly-true-$idx.err"; then
        fail "execve anomaly trigger failed; see $RESULT_DIR/anomaly-true-$idx.err"
    fi
done

anomaly_ready="false"
for _ in $(seq 1 40); do
    if grep -q '"operation":"anomaly"' "$ROOT/reports/events/security_policy.jsonl" 2>/dev/null && \
        grep -q '"rule_id":"burst_execve"' "$ROOT/reports/events/security_policy.jsonl" 2>/dev/null && \
        grep -q '"event_hook":"sys_enter_execve"' "$ROOT/reports/events/security_policy.jsonl" 2>/dev/null && \
        grep -q '"result":"observed"' "$ROOT/reports/events/security_policy.jsonl" 2>/dev/null; then
        anomaly_ready="true"
        break
    fi
    sleep 0.1
done

set +e
wait "$AGENT_PID"
agent_rc="$?"
set -e
AGENT_PID=""
if [ "$agent_rc" -ne 0 ]; then
    fail "anomaly agent exited non-zero, rc=$agent_rc; see $RESULT_DIR/agent-anomaly.log"
fi
if [ "$anomaly_ready" != "true" ]; then
    fail "burst_execve anomaly event was not observed; see $RESULT_DIR/agent-anomaly.log"
fi
if ! grep -q '"threshold":"4"' "$ROOT/reports/events/security_policy.jsonl" 2>/dev/null || \
    ! grep -q '"window_ms":"2000"' "$ROOT/reports/events/security_policy.jsonl" 2>/dev/null; then
    fail "burst_execve anomaly event did not carry threshold/window evidence"
fi
cp "$ROOT/reports/events/security_policy.jsonl" "$RESULT_DIR/security_policy_events.anomaly-execve.jsonl"
log "PASS: security_policy detects configurable burst_execve anomaly"

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
        exec_path: /root/EulerPilot/demo/security_policy_demo/deny_exec.sh
    rules:
      - name: deny_demo_secret_open
        hook: lsm_file_open
        target_ref: demo_secret
        action: deny
      - name: deny_demo_exec
        hook: lsm_bprm_check_security
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

exec_blocked="false"
set +e
"$EXEC_TARGET_FILE" > "$RESULT_DIR/blocked-exec.txt" 2> "$RESULT_DIR/blocked-exec.err"
exec_rc="$?"
set -e
if [ "$exec_rc" -ne 0 ]; then
    exec_blocked="true"
fi
if [ "$exec_blocked" != "true" ]; then
    fail "exec target was not denied while security_policy enforce is active; see $RESULT_DIR/blocked-exec.txt"
fi
log "PASS: exec target is denied while security_policy enforce is active"

if ! grep -Eqi "Operation not permitted|Permission denied|权限|不允许" "$RESULT_DIR/blocked-secret.err"; then
    log "WARN: denial stderr did not contain the usual permission text; see $RESULT_DIR/blocked-secret.err"
fi
if ! grep -Eqi "Operation not permitted|Permission denied|权限|不允许" "$RESULT_DIR/blocked-exec.err"; then
    log "WARN: exec denial stderr did not contain the usual permission text; see $RESULT_DIR/blocked-exec.err"
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
if ! grep -q '"event_hook":"lsm_bprm_check_security"' "$ROOT/reports/events/security_policy.jsonl" 2>/dev/null; then
    fail "enforce mode did not write bprm_check_security hit event"
fi
if ! grep -q '"result":"blocked"' "$ROOT/reports/events/security_policy.jsonl" 2>/dev/null; then
    fail "enforce mode hit event was not blocked"
fi
cp "$ROOT/reports/events/security_policy.jsonl" "$RESULT_DIR/security_policy_events.enforce.jsonl"
log "PASS: security_policy enforce mode writes blocked file and bprm hit events"

run_cleanup_script

if ! cat "$TARGET_FILE" > "$RESULT_DIR/post-cleanup-secret.txt" 2> "$RESULT_DIR/post-cleanup-secret.err"; then
    fail "target file is still denied after rollback/cleanup; see $RESULT_DIR/post-cleanup-secret.err"
fi
log "PASS: target file is readable after agent rollback"

if ! "$EXEC_TARGET_FILE" > "$RESULT_DIR/post-cleanup-exec.txt" 2> "$RESULT_DIR/post-cleanup-exec.err"; then
    fail "exec target is still denied after rollback/cleanup; see $RESULT_DIR/post-cleanup-exec.err"
fi
log "PASS: exec target is runnable after agent rollback"

DYNAMIC_DIR="$(mktemp -d /tmp/eulerpilot-security-policy.XXXXXX)"
DYNAMIC_TARGET_FILE="$DYNAMIC_DIR/dynamic-secret.txt"
DYNAMIC_EXEC_TARGET_FILE="$DYNAMIC_DIR/dynamic-deny-exec.sh"
DYNAMIC_SECOND_TARGET_FILE="$DYNAMIC_DIR/dynamic-second-secret.txt"
DYNAMIC_SECOND_EXEC_TARGET_FILE="$DYNAMIC_DIR/dynamic-second-deny-exec.sh"
DYNAMIC_SCOPED_TARGET_FILE="$DYNAMIC_DIR/dynamic-scoped-secret.txt"
DYNAMIC_SCOPED_EXEC_TARGET_FILE="$DYNAMIC_DIR/dynamic-scoped-deny-exec.sh"
DYNAMIC_PREFIX_EXEC_FILE="$DYNAMIC_DIR/prefix-deny-exec.sh"
DYNAMIC_WRITE_TARGET_FILE="$DYNAMIC_DIR/write-only-target.txt"
printf 'dynamic security policy target\n' > "$DYNAMIC_TARGET_FILE"
printf 'second dynamic security policy target\n' > "$DYNAMIC_SECOND_TARGET_FILE"
printf 'scoped dynamic security policy target\n' > "$DYNAMIC_SCOPED_TARGET_FILE"
printf 'write scoped target\n' > "$DYNAMIC_WRITE_TARGET_FILE"
cat > "$DYNAMIC_EXEC_TARGET_FILE" <<'SH'
#!/usr/bin/env bash
echo dynamic security policy exec target
SH
cat > "$DYNAMIC_SECOND_EXEC_TARGET_FILE" <<'SH'
#!/usr/bin/env bash
echo second dynamic security policy exec target
SH
cat > "$DYNAMIC_SCOPED_EXEC_TARGET_FILE" <<'SH'
#!/usr/bin/env bash
echo scoped dynamic security policy exec target
SH
cat > "$DYNAMIC_PREFIX_EXEC_FILE" <<'SH'
#!/usr/bin/env bash
echo writable directory prefix exec target
SH
chmod +x "$DYNAMIC_EXEC_TARGET_FILE" "$DYNAMIC_SECOND_EXEC_TARGET_FILE" \
    "$DYNAMIC_SCOPED_EXEC_TARGET_FILE" "$DYNAMIC_PREFIX_EXEC_FILE"

cat > "$RESULT_DIR/agent.dynamic.yaml" <<'YAML'
skills_config_path: skills.dynamic.yaml
exporter:
  prometheus:
    enabled: false
YAML

cat > "$RESULT_DIR/skills.dynamic.yaml" <<YAML
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
      dynamic_secret:
        type: path
        path: $DYNAMIC_TARGET_FILE
        exec_path: $DYNAMIC_EXEC_TARGET_FILE
      dynamic_second:
        type: path
        path: $DYNAMIC_SECOND_TARGET_FILE
        exec_path: $DYNAMIC_SECOND_EXEC_TARGET_FILE
    rules:
      - name: deny_dynamic_secret_open
        hook: lsm_file_open
        target_ref: dynamic_secret
        action: deny
      - name: deny_dynamic_exec
        hook: lsm_bprm_check_security
        target_ref: dynamic_secret
        action: deny
      - name: deny_dynamic_second_open
        hook: lsm_file_open
        target_ref: dynamic_second
        action: deny
      - name: deny_dynamic_second_exec
        hook: lsm_bprm_check_security
        target_ref: dynamic_second
        action: deny
YAML

rm -f "$ROOT/reports/events/security_policy.jsonl"

timeout 20s "$AGENT_BIN" \
    --config "$RESULT_DIR/agent.dynamic.yaml" \
    --duration-s 8 \
    --interval-ms 1000 \
    --jsonl \
    > "$RESULT_DIR/agent-dynamic.log" 2>&1 &
AGENT_PID="$!"

dynamic_blocked="false"
for _ in $(seq 1 40); do
    if ! kill -0 "$AGENT_PID" 2>/dev/null; then
        set +e
        wait "$AGENT_PID"
        agent_rc="$?"
        set -e
        AGENT_PID=""
        fail "dynamic target agent exited before denial was observed, rc=$agent_rc; see $RESULT_DIR/agent-dynamic.log"
    fi

    set +e
    cat "$DYNAMIC_TARGET_FILE" > "$RESULT_DIR/dynamic-blocked-secret.txt" 2> "$RESULT_DIR/dynamic-blocked-secret.err"
    cat_rc="$?"
    set -e
    if [ "$cat_rc" -ne 0 ]; then
        dynamic_blocked="true"
        break
    fi
    sleep 0.2
done

if [ "$dynamic_blocked" != "true" ]; then
    fail "dynamic target file was not denied; see $RESULT_DIR/agent-dynamic.log"
fi

dynamic_second_blocked="false"
for _ in $(seq 1 40); do
    if ! kill -0 "$AGENT_PID" 2>/dev/null; then
        set +e
        wait "$AGENT_PID"
        agent_rc="$?"
        set -e
        AGENT_PID=""
        fail "multi-target agent exited before second denial was observed, rc=$agent_rc; see $RESULT_DIR/agent-dynamic.log"
    fi

    set +e
    cat "$DYNAMIC_SECOND_TARGET_FILE" > "$RESULT_DIR/dynamic-second-blocked-secret.txt" 2> "$RESULT_DIR/dynamic-second-blocked-secret.err"
    cat_rc="$?"
    set -e
    if [ "$cat_rc" -ne 0 ]; then
        dynamic_second_blocked="true"
        break
    fi
    sleep 0.2
done

if [ "$dynamic_second_blocked" != "true" ]; then
    fail "second dynamic target file was not denied; see $RESULT_DIR/agent-dynamic.log"
fi

if ! cat "$TARGET_FILE" > "$RESULT_DIR/dynamic-default-secret.txt" 2> "$RESULT_DIR/dynamic-default-secret.err"; then
    fail "default demo target was denied while dynamic target_map was active; see $RESULT_DIR/dynamic-default-secret.err"
fi
if ! "$EXEC_TARGET_FILE" > "$RESULT_DIR/dynamic-default-exec.txt" 2> "$RESULT_DIR/dynamic-default-exec.err"; then
    fail "default demo exec target was denied while dynamic target_map was active; see $RESULT_DIR/dynamic-default-exec.err"
fi

set +e
"$DYNAMIC_EXEC_TARGET_FILE" > "$RESULT_DIR/dynamic-blocked-exec.txt" 2> "$RESULT_DIR/dynamic-blocked-exec.err"
dynamic_exec_rc="$?"
set -e
if [ "$dynamic_exec_rc" -eq 0 ]; then
    fail "dynamic exec target was not denied; see $RESULT_DIR/dynamic-blocked-exec.txt"
fi
set +e
"$DYNAMIC_SECOND_EXEC_TARGET_FILE" > "$RESULT_DIR/dynamic-second-blocked-exec.txt" 2> "$RESULT_DIR/dynamic-second-blocked-exec.err"
dynamic_second_exec_rc="$?"
set -e
if [ "$dynamic_second_exec_rc" -eq 0 ]; then
    fail "second dynamic exec target was not denied; see $RESULT_DIR/dynamic-second-blocked-exec.txt"
fi

set +e
wait "$AGENT_PID"
agent_rc="$?"
set -e
AGENT_PID=""
if [ "$agent_rc" -ne 0 ]; then
    fail "dynamic target agent exited non-zero, rc=$agent_rc; see $RESULT_DIR/agent-dynamic.log"
fi

if ! grep -Fq "$DYNAMIC_TARGET_FILE" "$ROOT/reports/events/security_policy.jsonl" 2>/dev/null; then
    fail "dynamic target file path was not present in security_policy events"
fi
if ! grep -Fq "$DYNAMIC_EXEC_TARGET_FILE" "$ROOT/reports/events/security_policy.jsonl" 2>/dev/null; then
    fail "dynamic exec target path was not present in security_policy events"
fi
if ! grep -Fq "$DYNAMIC_SECOND_TARGET_FILE" "$ROOT/reports/events/security_policy.jsonl" 2>/dev/null; then
    fail "second dynamic target file path was not present in security_policy events"
fi
if ! grep -Fq "$DYNAMIC_SECOND_EXEC_TARGET_FILE" "$ROOT/reports/events/security_policy.jsonl" 2>/dev/null; then
    fail "second dynamic exec target path was not present in security_policy events"
fi
if ! grep -q '"result":"blocked"' "$ROOT/reports/events/security_policy.jsonl" 2>/dev/null; then
    fail "dynamic target_map test did not write blocked events"
fi
assert_blocked_rule_event "$DYNAMIC_TARGET_FILE" "lsm_file_open" \
    "deny_dynamic_secret_open" "dynamic_secret"
assert_blocked_rule_event "$DYNAMIC_EXEC_TARGET_FILE" "lsm_bprm_check_security" \
    "deny_dynamic_exec" "dynamic_secret"
assert_blocked_rule_event "$DYNAMIC_SECOND_TARGET_FILE" "lsm_file_open" \
    "deny_dynamic_second_open" "dynamic_second"
assert_blocked_rule_event "$DYNAMIC_SECOND_EXEC_TARGET_FILE" "lsm_bprm_check_security" \
    "deny_dynamic_second_exec" "dynamic_second"
cp "$ROOT/reports/events/security_policy.jsonl" "$RESULT_DIR/security_policy_events.dynamic.jsonl"
log "PASS: security_policy target_map reports rule-specific dynamic YAML file and exec hits"

run_cleanup_script

if ! cat "$DYNAMIC_TARGET_FILE" > "$RESULT_DIR/dynamic-post-cleanup-secret.txt" 2> "$RESULT_DIR/dynamic-post-cleanup-secret.err"; then
    fail "dynamic target file is still denied after rollback/cleanup; see $RESULT_DIR/dynamic-post-cleanup-secret.err"
fi
if ! "$DYNAMIC_EXEC_TARGET_FILE" > "$RESULT_DIR/dynamic-post-cleanup-exec.txt" 2> "$RESULT_DIR/dynamic-post-cleanup-exec.err"; then
    fail "dynamic exec target is still denied after rollback/cleanup; see $RESULT_DIR/dynamic-post-cleanup-exec.err"
fi
if ! cat "$DYNAMIC_SECOND_TARGET_FILE" > "$RESULT_DIR/dynamic-second-post-cleanup-secret.txt" 2> "$RESULT_DIR/dynamic-second-post-cleanup-secret.err"; then
    fail "second dynamic target file is still denied after rollback/cleanup; see $RESULT_DIR/dynamic-second-post-cleanup-secret.err"
fi
if ! "$DYNAMIC_SECOND_EXEC_TARGET_FILE" > "$RESULT_DIR/dynamic-second-post-cleanup-exec.txt" 2> "$RESULT_DIR/dynamic-second-post-cleanup-exec.err"; then
    fail "second dynamic exec target is still denied after rollback/cleanup; see $RESULT_DIR/dynamic-second-post-cleanup-exec.err"
fi

SCOPED_CGROUP_PATH="/sys/fs/cgroup/eulerpilot/security-scope-$$"
mkdir -p /sys/fs/cgroup/eulerpilot
mkdir "$SCOPED_CGROUP_PATH"

cat > "$RESULT_DIR/agent.scoped.yaml" <<'YAML'
skills_config_path: skills.scoped.yaml
exporter:
  prometheus:
    enabled: false
YAML

cat > "$RESULT_DIR/skills.scoped.yaml" <<YAML
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
      scoped_secret:
        type: path
        path: $DYNAMIC_SCOPED_TARGET_FILE
        exec_prefix: $DYNAMIC_DIR/
        cgroup_path: $SCOPED_CGROUP_PATH
    rules:
      - name: deny_scoped_secret_open
        hook: lsm_file_open
        target_ref: scoped_secret
        action: deny
      - name: deny_scoped_exec
        hook: lsm_bprm_check_security
        target_ref: scoped_secret
        action: deny
YAML

rm -f "$ROOT/reports/events/security_policy.jsonl"

timeout 20s "$AGENT_BIN" \
    --config "$RESULT_DIR/agent.scoped.yaml" \
    --duration-s 8 \
    --interval-ms 1000 \
    --jsonl \
    > "$RESULT_DIR/agent-scoped.log" 2>&1 &
AGENT_PID="$!"

scoped_blocked="false"
for _ in $(seq 1 40); do
    if ! kill -0 "$AGENT_PID" 2>/dev/null; then
        set +e
        wait "$AGENT_PID"
        agent_rc="$?"
        set -e
        AGENT_PID=""
        fail "scoped target agent exited before cgroup denial was observed, rc=$agent_rc; see $RESULT_DIR/agent-scoped.log"
    fi

    if ! cat "$DYNAMIC_SCOPED_TARGET_FILE" > "$RESULT_DIR/scoped-outside-secret.txt" 2> "$RESULT_DIR/scoped-outside-secret.err"; then
        fail "scoped target file was denied outside target cgroup; see $RESULT_DIR/scoped-outside-secret.err"
    fi

    set +e
    run_in_scoped_cgroup "$RESULT_DIR/scoped-blocked-secret.txt" \
        "$RESULT_DIR/scoped-blocked-secret.err" \
        cat "$DYNAMIC_SCOPED_TARGET_FILE"
    cat_rc="$?"
    set -e
    if [ "$cat_rc" -ne 0 ]; then
        scoped_blocked="true"
        break
    fi
    sleep 0.2
done

if [ "$scoped_blocked" != "true" ]; then
    fail "scoped target file was not denied inside target cgroup; see $RESULT_DIR/agent-scoped.log"
fi

if ! "$DYNAMIC_SCOPED_EXEC_TARGET_FILE" > "$RESULT_DIR/scoped-outside-exec.txt" 2> "$RESULT_DIR/scoped-outside-exec.err"; then
    fail "scoped exec target was denied outside target cgroup; see $RESULT_DIR/scoped-outside-exec.err"
fi

set +e
run_in_scoped_cgroup "$RESULT_DIR/scoped-blocked-exec.txt" \
    "$RESULT_DIR/scoped-blocked-exec.err" \
    "$DYNAMIC_SCOPED_EXEC_TARGET_FILE"
scoped_exec_rc="$?"
set -e
if [ "$scoped_exec_rc" -eq 0 ]; then
    fail "scoped exec target was not denied inside target cgroup; see $RESULT_DIR/scoped-blocked-exec.txt"
fi

set +e
wait "$AGENT_PID"
agent_rc="$?"
set -e
AGENT_PID=""
if [ "$agent_rc" -ne 0 ]; then
    fail "scoped target agent exited non-zero, rc=$agent_rc; see $RESULT_DIR/agent-scoped.log"
fi

assert_blocked_rule_event "$DYNAMIC_SCOPED_TARGET_FILE" "lsm_file_open" \
    "deny_scoped_secret_open" "scoped_secret"
assert_blocked_rule_event "$DYNAMIC_SCOPED_EXEC_TARGET_FILE" "lsm_bprm_check_security" \
    "deny_scoped_exec" "scoped_secret"
assert_blocked_rule_event_has_cgroup "$DYNAMIC_SCOPED_TARGET_FILE" "lsm_file_open" \
    "deny_scoped_secret_open" "scoped_secret"
cp "$ROOT/reports/events/security_policy.jsonl" "$RESULT_DIR/security_policy_events.scoped.jsonl"
log "PASS: security_policy cgroup scoped target only blocks inside target cgroup"

run_cleanup_script

if ! run_in_scoped_cgroup "$RESULT_DIR/scoped-post-cleanup-secret.txt" \
    "$RESULT_DIR/scoped-post-cleanup-secret.err" \
    cat "$DYNAMIC_SCOPED_TARGET_FILE"; then
    fail "scoped target file is still denied after rollback/cleanup; see $RESULT_DIR/scoped-post-cleanup-secret.err"
fi
if ! run_in_scoped_cgroup "$RESULT_DIR/scoped-post-cleanup-exec.txt" \
    "$RESULT_DIR/scoped-post-cleanup-exec.err" \
    "$DYNAMIC_SCOPED_EXEC_TARGET_FILE"; then
    fail "scoped exec target is still denied after rollback/cleanup; see $RESULT_DIR/scoped-post-cleanup-exec.err"
fi

SOCKET_PORT=$((19000 + ($$ % 2000)))
start_socket_server "$SOCKET_PORT"

cat > "$RESULT_DIR/agent.socket.yaml" <<'YAML'
skills_config_path: skills.socket.yaml
exporter:
  prometheus:
    enabled: false
YAML

cat > "$RESULT_DIR/skills.socket.yaml" <<YAML
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
      socket_scope:
        type: cgroup
        cgroup_path: $SCOPED_CGROUP_PATH
        dst_ip: 127.0.0.1
        dst_port: $SOCKET_PORT
    rules:
      - name: deny_socket_connect
        hook: lsm_socket_connect
        target_ref: socket_scope
        action: deny
YAML

rm -f "$ROOT/reports/events/security_policy.jsonl"

timeout 20s "$AGENT_BIN" \
    --config "$RESULT_DIR/agent.socket.yaml" \
    --duration-s 8 \
    --interval-ms 1000 \
    --jsonl \
    > "$RESULT_DIR/agent-socket.log" 2>&1 &
AGENT_PID="$!"

socket_blocked="false"
for _ in $(seq 1 40); do
    if ! kill -0 "$AGENT_PID" 2>/dev/null; then
        set +e
        wait "$AGENT_PID"
        agent_rc="$?"
        set -e
        AGENT_PID=""
        fail "socket target agent exited before connect denial was observed, rc=$agent_rc; see $RESULT_DIR/agent-socket.log"
    fi

    if ! python3 "$RESULT_DIR/connect_ipv4.py" "$SOCKET_PORT" \
        > "$RESULT_DIR/socket-outside.txt" 2> "$RESULT_DIR/socket-outside.err"; then
        fail "socket connect was denied outside target cgroup; see $RESULT_DIR/socket-outside.err"
    fi

    set +e
    run_in_scoped_cgroup "$RESULT_DIR/socket-blocked.txt" \
        "$RESULT_DIR/socket-blocked.err" \
        python3 "$RESULT_DIR/connect_ipv4.py" "$SOCKET_PORT"
    socket_rc="$?"
    set -e
    if [ "$socket_rc" -ne 0 ]; then
        socket_blocked="true"
        break
    fi
    sleep 0.2
done

if [ "$socket_blocked" != "true" ]; then
    fail "socket connect was not denied inside target cgroup; see $RESULT_DIR/agent-socket.log"
fi

set +e
wait "$AGENT_PID"
agent_rc="$?"
set -e
AGENT_PID=""
if [ "$agent_rc" -ne 0 ]; then
    fail "socket target agent exited non-zero, rc=$agent_rc; see $RESULT_DIR/agent-socket.log"
fi

assert_blocked_rule_event "socket_connect" "lsm_socket_connect" \
    "deny_socket_connect" "socket_scope"
if ! grep -F "socket_connect" "$ROOT/reports/events/security_policy.jsonl" 2>/dev/null \
    | grep -F '"event_hook":"lsm_socket_connect"' \
    | grep -F '"result":"blocked"' \
    | grep -F '"dst_ip":"127.0.0.1"' \
    | grep -Fq "\"dst_port\":\"$SOCKET_PORT\""; then
    fail "socket connect blocked event did not carry dst_ip/dst_port evidence"
fi
cp "$ROOT/reports/events/security_policy.jsonl" "$RESULT_DIR/security_policy_events.socket.jsonl"
log "PASS: security_policy lsm_socket_connect blocks scoped IPv4 target and reports endpoint evidence"

run_cleanup_script

if ! run_in_scoped_cgroup "$RESULT_DIR/socket-post-cleanup.txt" \
    "$RESULT_DIR/socket-post-cleanup.err" \
    python3 "$RESULT_DIR/connect_ipv4.py" "$SOCKET_PORT"; then
    fail "socket connect is still denied after rollback/cleanup; see $RESULT_DIR/socket-post-cleanup.err"
fi
cleanup_socket_server

cat > "$RESULT_DIR/agent.exec-prefix.yaml" <<'YAML'
skills_config_path: skills.exec-prefix.yaml
exporter:
  prometheus:
    enabled: false
YAML

cat > "$RESULT_DIR/skills.exec-prefix.yaml" <<YAML
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
      writable_exec:
        type: cgroup
        cgroup_path: $SCOPED_CGROUP_PATH
        exec_prefix: $DYNAMIC_DIR/
    rules:
      - name: deny_writable_dir_exec
        hook: lsm_bprm_check_security
        target_ref: writable_exec
        action: deny
YAML

rm -f "$ROOT/reports/events/security_policy.jsonl"

timeout 20s "$AGENT_BIN" \
    --config "$RESULT_DIR/agent.exec-prefix.yaml" \
    --duration-s 8 \
    --interval-ms 1000 \
    --jsonl \
    > "$RESULT_DIR/agent-exec-prefix.log" 2>&1 &
AGENT_PID="$!"

prefix_blocked="false"
for _ in $(seq 1 40); do
    if ! kill -0 "$AGENT_PID" 2>/dev/null; then
        set +e
        wait "$AGENT_PID"
        agent_rc="$?"
        set -e
        AGENT_PID=""
        fail "exec-prefix agent exited before bprm denial was observed, rc=$agent_rc; see $RESULT_DIR/agent-exec-prefix.log"
    fi

    if ! "$DYNAMIC_PREFIX_EXEC_FILE" > "$RESULT_DIR/exec-prefix-outside.txt" \
        2> "$RESULT_DIR/exec-prefix-outside.err"; then
        fail "exec-prefix target was denied outside target cgroup; see $RESULT_DIR/exec-prefix-outside.err"
    fi

    set +e
    run_in_scoped_cgroup "$RESULT_DIR/exec-prefix-blocked.txt" \
        "$RESULT_DIR/exec-prefix-blocked.err" \
        "$DYNAMIC_PREFIX_EXEC_FILE"
    prefix_rc="$?"
    set -e
    if [ "$prefix_rc" -ne 0 ]; then
        prefix_blocked="true"
        break
    fi
    sleep 0.2
done

if [ "$prefix_blocked" != "true" ]; then
    fail "exec-prefix target was not denied inside target cgroup; see $RESULT_DIR/agent-exec-prefix.log"
fi

set +e
wait "$AGENT_PID"
agent_rc="$?"
set -e
AGENT_PID=""
if [ "$agent_rc" -ne 0 ]; then
    fail "exec-prefix agent exited non-zero, rc=$agent_rc; see $RESULT_DIR/agent-exec-prefix.log"
fi

assert_blocked_rule_event "$DYNAMIC_PREFIX_EXEC_FILE" "lsm_bprm_check_security" \
    "deny_writable_dir_exec" "writable_exec"
assert_blocked_rule_event_has_cgroup "$DYNAMIC_PREFIX_EXEC_FILE" "lsm_bprm_check_security" \
    "deny_writable_dir_exec" "writable_exec"
if ! grep -F "$DYNAMIC_PREFIX_EXEC_FILE" "$ROOT/reports/events/security_policy.jsonl" 2>/dev/null \
    | grep -F '"event_hook":"lsm_bprm_check_security"' \
    | grep -F '"result":"blocked"' \
    | grep -Fq "\"exec_prefix\":\"$DYNAMIC_DIR/\""; then
    fail "exec-prefix blocked event did not carry exec_prefix evidence"
fi
cp "$ROOT/reports/events/security_policy.jsonl" "$RESULT_DIR/security_policy_events.exec-prefix.jsonl"
log "PASS: security_policy bprm exec_prefix blocks writable-dir execution inside target cgroup"

run_cleanup_script

if ! run_in_scoped_cgroup "$RESULT_DIR/exec-prefix-post-cleanup.txt" \
    "$RESULT_DIR/exec-prefix-post-cleanup.err" \
    "$DYNAMIC_PREFIX_EXEC_FILE"; then
    fail "exec-prefix target is still denied after rollback/cleanup; see $RESULT_DIR/exec-prefix-post-cleanup.err"
fi

cat > "$RESULT_DIR/agent.file-access.yaml" <<'YAML'
skills_config_path: skills.file-access.yaml
exporter:
  prometheus:
    enabled: false
YAML

cat > "$RESULT_DIR/skills.file-access.yaml" <<YAML
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
      write_secret:
        type: cgroup
        cgroup_path: $SCOPED_CGROUP_PATH
        path: $DYNAMIC_WRITE_TARGET_FILE
        file_access: write
    rules:
      - name: deny_write_open
        hook: lsm_file_open
        target_ref: write_secret
        action: deny
YAML

rm -f "$ROOT/reports/events/security_policy.jsonl"

timeout 20s "$AGENT_BIN" \
    --config "$RESULT_DIR/agent.file-access.yaml" \
    --duration-s 8 \
    --interval-ms 1000 \
    --jsonl \
    > "$RESULT_DIR/agent-file-access.log" 2>&1 &
AGENT_PID="$!"

write_blocked="false"
for _ in $(seq 1 40); do
    if ! kill -0 "$AGENT_PID" 2>/dev/null; then
        set +e
        wait "$AGENT_PID"
        agent_rc="$?"
        set -e
        AGENT_PID=""
        fail "file_access agent exited before write denial was observed, rc=$agent_rc; see $RESULT_DIR/agent-file-access.log"
    fi

    if ! printf 'outside write\n' >> "$DYNAMIC_WRITE_TARGET_FILE"; then
        fail "file_access target write was denied outside target cgroup"
    fi

    if ! run_in_scoped_cgroup "$RESULT_DIR/file-access-read.txt" \
        "$RESULT_DIR/file-access-read.err" \
        cat "$DYNAMIC_WRITE_TARGET_FILE"; then
        fail "file_access=write denied read inside target cgroup; see $RESULT_DIR/file-access-read.err"
    fi

    set +e
    run_in_scoped_cgroup "$RESULT_DIR/file-access-write.txt" \
        "$RESULT_DIR/file-access-write.err" \
        bash -c 'printf "inside scoped write\n" >> "$1"' _ "$DYNAMIC_WRITE_TARGET_FILE"
    write_rc="$?"
    set -e
    if [ "$write_rc" -ne 0 ]; then
        write_blocked="true"
        break
    fi
    sleep 0.2
done

if [ "$write_blocked" != "true" ]; then
    fail "file_access=write target was not denied inside target cgroup; see $RESULT_DIR/agent-file-access.log"
fi

set +e
wait "$AGENT_PID"
agent_rc="$?"
set -e
AGENT_PID=""
if [ "$agent_rc" -ne 0 ]; then
    fail "file_access agent exited non-zero, rc=$agent_rc; see $RESULT_DIR/agent-file-access.log"
fi

assert_blocked_rule_event "$DYNAMIC_WRITE_TARGET_FILE" "lsm_file_open" \
    "deny_write_open" "write_secret"
assert_blocked_rule_event_has_cgroup "$DYNAMIC_WRITE_TARGET_FILE" "lsm_file_open" \
    "deny_write_open" "write_secret"
if ! grep -F "$DYNAMIC_WRITE_TARGET_FILE" "$ROOT/reports/events/security_policy.jsonl" 2>/dev/null \
    | grep -F '"event_hook":"lsm_file_open"' \
    | grep -F '"result":"blocked"' \
    | grep -F '"file_access":"write"' \
    | grep -Fq '"file_flags":"'; then
    fail "file_access blocked event did not carry write access and file_flags evidence"
fi
cp "$ROOT/reports/events/security_policy.jsonl" "$RESULT_DIR/security_policy_events.file-access.jsonl"
log "PASS: security_policy file_access=write only blocks write opens inside target cgroup"

run_cleanup_script

if ! run_in_scoped_cgroup "$RESULT_DIR/file-access-post-cleanup.txt" \
    "$RESULT_DIR/file-access-post-cleanup.err" \
    bash -c 'printf "post cleanup write\n" >> "$1"' _ "$DYNAMIC_WRITE_TARGET_FILE"; then
    fail "file_access target is still denied after rollback/cleanup; see $RESULT_DIR/file-access-post-cleanup.err"
fi

DYNAMIC_READONLY_DIR="$DYNAMIC_DIR/read-only-dir"
DYNAMIC_READONLY_FILE="$DYNAMIC_READONLY_DIR/nested-secret.txt"
DYNAMIC_READONLY_PREFIX="$DYNAMIC_READONLY_DIR/"
mkdir -p "$DYNAMIC_READONLY_DIR"
printf 'read only directory target\n' > "$DYNAMIC_READONLY_FILE"

cat > "$RESULT_DIR/agent.readonly-dir.yaml" <<'YAML'
skills_config_path: skills.readonly-dir.yaml
exporter:
  prometheus:
    enabled: false
YAML

cat > "$RESULT_DIR/skills.readonly-dir.yaml" <<YAML
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
      readonly_dir:
        type: cgroup
        cgroup_path: $SCOPED_CGROUP_PATH
        path_prefix: $DYNAMIC_READONLY_PREFIX
        file_access: write
    rules:
      - name: deny_readonly_dir_write
        hook: lsm_file_open
        target_ref: readonly_dir
        action: deny
YAML

rm -f "$ROOT/reports/events/security_policy.jsonl"

timeout 20s "$AGENT_BIN" \
    --config "$RESULT_DIR/agent.readonly-dir.yaml" \
    --duration-s 8 \
    --interval-ms 1000 \
    --jsonl \
    > "$RESULT_DIR/agent-readonly-dir.log" 2>&1 &
AGENT_PID="$!"

readonly_dir_blocked="false"
for _ in $(seq 1 40); do
    if ! kill -0 "$AGENT_PID" 2>/dev/null; then
        set +e
        wait "$AGENT_PID"
        agent_rc="$?"
        set -e
        AGENT_PID=""
        fail "readonly-dir agent exited before write denial was observed, rc=$agent_rc; see $RESULT_DIR/agent-readonly-dir.log"
    fi

    if ! printf 'outside readonly dir write\n' >> "$DYNAMIC_READONLY_FILE"; then
        fail "readonly-dir target write was denied outside target cgroup"
    fi

    if ! run_in_scoped_cgroup "$RESULT_DIR/readonly-dir-read.txt" \
        "$RESULT_DIR/readonly-dir-read.err" \
        cat "$DYNAMIC_READONLY_FILE"; then
        fail "readonly-dir policy denied read inside target cgroup; see $RESULT_DIR/readonly-dir-read.err"
    fi

    set +e
    run_in_scoped_cgroup "$RESULT_DIR/readonly-dir-write.txt" \
        "$RESULT_DIR/readonly-dir-write.err" \
        bash -c 'printf "inside scoped readonly dir write\n" >> "$1"' _ "$DYNAMIC_READONLY_FILE"
    readonly_dir_rc="$?"
    set -e
    if [ "$readonly_dir_rc" -ne 0 ]; then
        readonly_dir_blocked="true"
        break
    fi
    sleep 0.2
done

if [ "$readonly_dir_blocked" != "true" ]; then
    fail "readonly-dir write was not denied inside target cgroup; see $RESULT_DIR/agent-readonly-dir.log"
fi

set +e
wait "$AGENT_PID"
agent_rc="$?"
set -e
AGENT_PID=""
if [ "$agent_rc" -ne 0 ]; then
    fail "readonly-dir agent exited non-zero, rc=$agent_rc; see $RESULT_DIR/agent-readonly-dir.log"
fi

assert_blocked_rule_event "$DYNAMIC_READONLY_FILE" "lsm_file_open" \
    "deny_readonly_dir_write" "readonly_dir"
assert_blocked_rule_event_has_cgroup "$DYNAMIC_READONLY_FILE" "lsm_file_open" \
    "deny_readonly_dir_write" "readonly_dir"
if ! grep -F "$DYNAMIC_READONLY_FILE" "$ROOT/reports/events/security_policy.jsonl" 2>/dev/null \
    | grep -F '"event_hook":"lsm_file_open"' \
    | grep -F '"result":"blocked"' \
    | grep -F '"file_access":"write"' \
    | grep -Fq "\"path_prefix\":\"$DYNAMIC_READONLY_PREFIX\""; then
    fail "readonly-dir blocked event did not carry path_prefix evidence"
fi
cp "$ROOT/reports/events/security_policy.jsonl" "$RESULT_DIR/security_policy_events.readonly-dir.jsonl"
log "PASS: security_policy path_prefix read-only directory blocks scoped writes"

run_cleanup_script

if ! run_in_scoped_cgroup "$RESULT_DIR/readonly-dir-post-cleanup.txt" \
    "$RESULT_DIR/readonly-dir-post-cleanup.err" \
    bash -c 'printf "post cleanup readonly dir write\n" >> "$1"' _ "$DYNAMIC_READONLY_FILE"; then
    fail "readonly-dir target is still denied after rollback/cleanup; see $RESULT_DIR/readonly-dir-post-cleanup.err"
fi

cat > "$RESULT_DIR/ptrace_traceme.py" <<'PY'
import ctypes
import errno
import os
import sys

libc = ctypes.CDLL(None, use_errno=True)
rc = libc.ptrace(0, 0, None, None)
if rc != 0:
    err = ctypes.get_errno()
    print(os.strerror(err or errno.EPERM), file=sys.stderr)
    sys.exit(1)
PY

cat > "$RESULT_DIR/agent.ptrace.yaml" <<'YAML'
skills_config_path: skills.ptrace.yaml
exporter:
  prometheus:
    enabled: false
YAML

cat > "$RESULT_DIR/skills.ptrace.yaml" <<YAML
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
      ptrace_scope:
        type: cgroup
        cgroup_path: $SCOPED_CGROUP_PATH
    rules:
      - name: deny_ptrace_traceme
        hook: lsm_ptrace_traceme
        target_ref: ptrace_scope
        action: deny
YAML

rm -f "$ROOT/reports/events/security_policy.jsonl"

timeout 20s "$AGENT_BIN" \
    --config "$RESULT_DIR/agent.ptrace.yaml" \
    --duration-s 8 \
    --interval-ms 1000 \
    --jsonl \
    > "$RESULT_DIR/agent-ptrace.log" 2>&1 &
AGENT_PID="$!"

ptrace_blocked="false"
for _ in $(seq 1 40); do
    if ! kill -0 "$AGENT_PID" 2>/dev/null; then
        set +e
        wait "$AGENT_PID"
        agent_rc="$?"
        set -e
        AGENT_PID=""
        fail "ptrace agent exited before denial was observed, rc=$agent_rc; see $RESULT_DIR/agent-ptrace.log"
    fi

    if ! python3 "$RESULT_DIR/ptrace_traceme.py" > "$RESULT_DIR/ptrace-outside.txt" \
        2> "$RESULT_DIR/ptrace-outside.err"; then
        fail "ptrace_traceme was denied outside target cgroup; see $RESULT_DIR/ptrace-outside.err"
    fi

    set +e
    run_in_scoped_cgroup "$RESULT_DIR/ptrace-blocked.txt" \
        "$RESULT_DIR/ptrace-blocked.err" \
        python3 "$RESULT_DIR/ptrace_traceme.py"
    ptrace_rc="$?"
    set -e
    if [ "$ptrace_rc" -ne 0 ]; then
        ptrace_blocked="true"
        break
    fi
    sleep 0.2
done

if [ "$ptrace_blocked" != "true" ]; then
    fail "ptrace_traceme was not denied inside target cgroup; see $RESULT_DIR/agent-ptrace.log"
fi

set +e
wait "$AGENT_PID"
agent_rc="$?"
set -e
AGENT_PID=""
if [ "$agent_rc" -ne 0 ]; then
    fail "ptrace agent exited non-zero, rc=$agent_rc; see $RESULT_DIR/agent-ptrace.log"
fi

assert_blocked_rule_event "ptrace_traceme" "lsm_ptrace_traceme" \
    "deny_ptrace_traceme" "ptrace_scope"
assert_blocked_rule_event_has_cgroup "ptrace_traceme" "lsm_ptrace_traceme" \
    "deny_ptrace_traceme" "ptrace_scope"
cp "$ROOT/reports/events/security_policy.jsonl" "$RESULT_DIR/security_policy_events.ptrace.jsonl"
log "PASS: security_policy ptrace_traceme blocks scoped ptrace in target cgroup"

run_cleanup_script

if ! run_in_scoped_cgroup "$RESULT_DIR/ptrace-post-cleanup.txt" \
    "$RESULT_DIR/ptrace-post-cleanup.err" \
    python3 "$RESULT_DIR/ptrace_traceme.py"; then
    fail "ptrace_traceme is still denied after rollback/cleanup; see $RESULT_DIR/ptrace-post-cleanup.err"
fi

if ! unshare -m true > "$RESULT_DIR/capable-baseline.txt" \
    2> "$RESULT_DIR/capable-baseline.err"; then
    fail "CAP_SYS_ADMIN baseline unshare failed; see $RESULT_DIR/capable-baseline.err"
fi

cat > "$RESULT_DIR/agent.capable.yaml" <<'YAML'
skills_config_path: skills.capable.yaml
exporter:
  prometheus:
    enabled: false
YAML

cat > "$RESULT_DIR/skills.capable.yaml" <<YAML
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
      capable_scope:
        type: cgroup
        cgroup_path: $SCOPED_CGROUP_PATH
        capability: CAP_SYS_ADMIN
    rules:
      - name: deny_cap_sys_admin
        hook: lsm_capable
        target_ref: capable_scope
        action: deny
YAML

rm -f "$ROOT/reports/events/security_policy.jsonl"

timeout 20s "$AGENT_BIN" \
    --config "$RESULT_DIR/agent.capable.yaml" \
    --duration-s 8 \
    --interval-ms 1000 \
    --jsonl \
    > "$RESULT_DIR/agent-capable.log" 2>&1 &
AGENT_PID="$!"

capable_blocked="false"
for _ in $(seq 1 40); do
    if ! kill -0 "$AGENT_PID" 2>/dev/null; then
        set +e
        wait "$AGENT_PID"
        agent_rc="$?"
        set -e
        AGENT_PID=""
        fail "capable agent exited before denial was observed, rc=$agent_rc; see $RESULT_DIR/agent-capable.log"
    fi

    if ! unshare -m true > "$RESULT_DIR/capable-outside.txt" \
        2> "$RESULT_DIR/capable-outside.err"; then
        fail "CAP_SYS_ADMIN was denied outside target cgroup; see $RESULT_DIR/capable-outside.err"
    fi

    set +e
    run_in_scoped_cgroup "$RESULT_DIR/capable-blocked.txt" \
        "$RESULT_DIR/capable-blocked.err" \
        unshare -m true
    capable_rc="$?"
    set -e
    if [ "$capable_rc" -ne 0 ]; then
        capable_blocked="true"
        break
    fi
    sleep 0.2
done

if [ "$capable_blocked" != "true" ]; then
    fail "CAP_SYS_ADMIN was not denied inside target cgroup; see $RESULT_DIR/agent-capable.log"
fi

set +e
wait "$AGENT_PID"
agent_rc="$?"
set -e
AGENT_PID=""
if [ "$agent_rc" -ne 0 ]; then
    fail "capable agent exited non-zero, rc=$agent_rc; see $RESULT_DIR/agent-capable.log"
fi

assert_blocked_rule_event "capable" "lsm_capable" \
    "deny_cap_sys_admin" "capable_scope"
assert_blocked_rule_event_has_cgroup "capable" "lsm_capable" \
    "deny_cap_sys_admin" "capable_scope"
if ! grep -F "capable" "$ROOT/reports/events/security_policy.jsonl" 2>/dev/null \
    | grep -F '"event_hook":"lsm_capable"' \
    | grep -F '"capability":"CAP_SYS_ADMIN"' \
    | grep -Fq '"result":"blocked"'; then
    fail "capable blocked event did not carry CAP_SYS_ADMIN evidence"
fi
cp "$ROOT/reports/events/security_policy.jsonl" "$RESULT_DIR/security_policy_events.capable.jsonl"
log "PASS: security_policy lsm_capable blocks scoped CAP_SYS_ADMIN in target cgroup"

run_cleanup_script

if ! run_in_scoped_cgroup "$RESULT_DIR/capable-post-cleanup.txt" \
    "$RESULT_DIR/capable-post-cleanup.err" \
    unshare -m true; then
    fail "CAP_SYS_ADMIN is still denied after rollback/cleanup; see $RESULT_DIR/capable-post-cleanup.err"
fi

cat > "$RESULT_DIR/setuid_transition.py" <<'PY'
import errno
import os
import sys

target_uid = int(sys.argv[1]) if len(sys.argv) > 1 else 65534
try:
    os.setuid(target_uid)
except OSError as exc:
    print(os.strerror(exc.errno or errno.EPERM), file=sys.stderr)
    sys.exit(1)
sys.exit(0)
PY

if ! python3 "$RESULT_DIR/setuid_transition.py" 65534 \
    > "$RESULT_DIR/setuid-baseline.txt" \
    2> "$RESULT_DIR/setuid-baseline.err"; then
    fail "setuid baseline failed before policy; see $RESULT_DIR/setuid-baseline.err"
fi

cat > "$RESULT_DIR/agent.setuid.yaml" <<'YAML'
skills_config_path: skills.setuid.yaml
exporter:
  prometheus:
    enabled: false
YAML

cat > "$RESULT_DIR/skills.setuid.yaml" <<YAML
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
      setuid_scope:
        type: cgroup
        cgroup_path: $SCOPED_CGROUP_PATH
    rules:
      - name: deny_setuid_transition
        hook: lsm_task_fix_setuid
        target_ref: setuid_scope
        action: deny
YAML

rm -f "$ROOT/reports/events/security_policy.jsonl"

timeout 20s "$AGENT_BIN" \
    --config "$RESULT_DIR/agent.setuid.yaml" \
    --duration-s 8 \
    --interval-ms 1000 \
    --jsonl \
    > "$RESULT_DIR/agent-setuid.log" 2>&1 &
AGENT_PID="$!"

setuid_blocked="false"
for _ in $(seq 1 40); do
    if ! kill -0 "$AGENT_PID" 2>/dev/null; then
        set +e
        wait "$AGENT_PID"
        agent_rc="$?"
        set -e
        AGENT_PID=""
        fail "setuid agent exited before denial was observed, rc=$agent_rc; see $RESULT_DIR/agent-setuid.log"
    fi

    if ! python3 "$RESULT_DIR/setuid_transition.py" 65534 \
        > "$RESULT_DIR/setuid-outside.txt" \
        2> "$RESULT_DIR/setuid-outside.err"; then
        fail "setuid transition was denied outside target cgroup; see $RESULT_DIR/setuid-outside.err"
    fi

    set +e
    run_in_scoped_cgroup "$RESULT_DIR/setuid-blocked.txt" \
        "$RESULT_DIR/setuid-blocked.err" \
        python3 "$RESULT_DIR/setuid_transition.py" 65534
    setuid_rc="$?"
    set -e
    if [ "$setuid_rc" -ne 0 ]; then
        setuid_blocked="true"
        break
    fi
    sleep 0.2
done

if [ "$setuid_blocked" != "true" ]; then
    fail "setuid transition was not denied inside target cgroup; see $RESULT_DIR/agent-setuid.log"
fi

set +e
wait "$AGENT_PID"
agent_rc="$?"
set -e
AGENT_PID=""
if [ "$agent_rc" -ne 0 ]; then
    fail "setuid agent exited non-zero, rc=$agent_rc; see $RESULT_DIR/agent-setuid.log"
fi

assert_blocked_rule_event "task_fix_setuid" "lsm_task_fix_setuid" \
    "deny_setuid_transition" "setuid_scope"
assert_blocked_rule_event_has_cgroup "task_fix_setuid" "lsm_task_fix_setuid" \
    "deny_setuid_transition" "setuid_scope"
if ! grep -F "task_fix_setuid" "$ROOT/reports/events/security_policy.jsonl" 2>/dev/null \
    | grep -F '"event_hook":"lsm_task_fix_setuid"' \
    | grep -F '"result":"blocked"' \
    | grep -F '"uid":"65534"' \
    | grep -F '"euid":"65534"' \
    | grep -Fq '"setuid_flags":"'; then
    fail "setuid blocked event did not carry uid/euid/setuid_flags evidence"
fi
cp "$ROOT/reports/events/security_policy.jsonl" "$RESULT_DIR/security_policy_events.setuid.jsonl"
log "PASS: security_policy lsm_task_fix_setuid blocks scoped setuid transitions"

run_cleanup_script

if ! run_in_scoped_cgroup "$RESULT_DIR/setuid-post-cleanup.txt" \
    "$RESULT_DIR/setuid-post-cleanup.err" \
    python3 "$RESULT_DIR/setuid_transition.py" 65534; then
    fail "setuid transition is still denied after rollback/cleanup; see $RESULT_DIR/setuid-post-cleanup.err"
fi
cat > "$RESULT_DIR/setgid_transition.py" <<'PY'
import errno
import os
import sys

target_gid = int(sys.argv[1]) if len(sys.argv) > 1 else 65534
try:
    os.setgid(target_gid)
except OSError as exc:
    print(os.strerror(exc.errno or errno.EPERM), file=sys.stderr)
    sys.exit(1)
sys.exit(0)
PY

if ! python3 "$RESULT_DIR/setgid_transition.py" 65534 \
    > "$RESULT_DIR/setgid-baseline.txt" \
    2> "$RESULT_DIR/setgid-baseline.err"; then
    fail "setgid baseline failed before policy; see $RESULT_DIR/setgid-baseline.err"
fi

cat > "$RESULT_DIR/agent.setgid.yaml" <<'YAML'
skills_config_path: skills.setgid.yaml
exporter:
  prometheus:
    enabled: false
YAML

cat > "$RESULT_DIR/skills.setgid.yaml" <<YAML
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
      setgid_scope:
        type: cgroup
        cgroup_path: $SCOPED_CGROUP_PATH
    rules:
      - name: deny_setgid_transition
        hook: lsm_task_fix_setgid
        target_ref: setgid_scope
        action: deny
YAML

rm -f "$ROOT/reports/events/security_policy.jsonl"

timeout 20s "$AGENT_BIN" \
    --config "$RESULT_DIR/agent.setgid.yaml" \
    --duration-s 8 \
    --interval-ms 1000 \
    --jsonl \
    > "$RESULT_DIR/agent-setgid.log" 2>&1 &
AGENT_PID="$!"

setgid_blocked="false"
for _ in $(seq 1 40); do
    if ! kill -0 "$AGENT_PID" 2>/dev/null; then
        set +e
        wait "$AGENT_PID"
        agent_rc="$?"
        set -e
        AGENT_PID=""
        fail "setgid agent exited before denial was observed, rc=$agent_rc; see $RESULT_DIR/agent-setgid.log"
    fi

    if ! python3 "$RESULT_DIR/setgid_transition.py" 65534 \
        > "$RESULT_DIR/setgid-outside.txt" \
        2> "$RESULT_DIR/setgid-outside.err"; then
        fail "setgid transition was denied outside target cgroup; see $RESULT_DIR/setgid-outside.err"
    fi

    set +e
    run_in_scoped_cgroup "$RESULT_DIR/setgid-blocked.txt" \
        "$RESULT_DIR/setgid-blocked.err" \
        python3 "$RESULT_DIR/setgid_transition.py" 65534
    setgid_rc="$?"
    set -e
    if [ "$setgid_rc" -ne 0 ]; then
        setgid_blocked="true"
        break
    fi
    sleep 0.2
done

if [ "$setgid_blocked" != "true" ]; then
    fail "setgid transition was not denied inside target cgroup; see $RESULT_DIR/agent-setgid.log"
fi

set +e
wait "$AGENT_PID"
agent_rc="$?"
set -e
AGENT_PID=""
if [ "$agent_rc" -ne 0 ]; then
    fail "setgid agent exited non-zero, rc=$agent_rc; see $RESULT_DIR/agent-setgid.log"
fi

assert_blocked_rule_event "task_fix_setgid" "lsm_task_fix_setgid" \
    "deny_setgid_transition" "setgid_scope"
assert_blocked_rule_event_has_cgroup "task_fix_setgid" "lsm_task_fix_setgid" \
    "deny_setgid_transition" "setgid_scope"
if ! grep -F "task_fix_setgid" "$ROOT/reports/events/security_policy.jsonl" 2>/dev/null \
    | grep -F '"event_hook":"lsm_task_fix_setgid"' \
    | grep -F '"result":"blocked"' \
    | grep -F '"gid":"65534"' \
    | grep -F '"egid":"65534"' \
    | grep -Fq '"setgid_flags":"'; then
    fail "setgid blocked event did not carry gid/egid/setgid_flags evidence"
fi
cp "$ROOT/reports/events/security_policy.jsonl" "$RESULT_DIR/security_policy_events.setgid.jsonl"
log "PASS: security_policy lsm_task_fix_setgid blocks scoped setgid transitions"

run_cleanup_script

if ! run_in_scoped_cgroup "$RESULT_DIR/setgid-post-cleanup.txt" \
    "$RESULT_DIR/setgid-post-cleanup.err" \
    python3 "$RESULT_DIR/setgid_transition.py" 65534; then
    fail "setgid transition is still denied after rollback/cleanup; see $RESULT_DIR/setgid-post-cleanup.err"
fi
cat > "$RESULT_DIR/setgroups_transition.py" <<'PY'
import errno
import os
import sys

target_gid = int(sys.argv[1]) if len(sys.argv) > 1 else 65534
try:
    os.setgroups([target_gid])
except OSError as exc:
    print(os.strerror(exc.errno or errno.EPERM), file=sys.stderr)
    sys.exit(1)
sys.exit(0)
PY

if ! python3 "$RESULT_DIR/setgroups_transition.py" 65534 \
    > "$RESULT_DIR/setgroups-baseline.txt" \
    2> "$RESULT_DIR/setgroups-baseline.err"; then
    fail "setgroups baseline failed before policy; see $RESULT_DIR/setgroups-baseline.err"
fi

cat > "$RESULT_DIR/agent.setgroups.yaml" <<'YAML'
skills_config_path: skills.setgroups.yaml
exporter:
  prometheus:
    enabled: false
YAML

cat > "$RESULT_DIR/skills.setgroups.yaml" <<YAML
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
      setgroups_scope:
        type: cgroup
        cgroup_path: $SCOPED_CGROUP_PATH
    rules:
      - name: deny_setgroups_transition
        hook: lsm_task_fix_setgroups
        target_ref: setgroups_scope
        action: deny
YAML

rm -f "$ROOT/reports/events/security_policy.jsonl"

timeout 20s "$AGENT_BIN" \
    --config "$RESULT_DIR/agent.setgroups.yaml" \
    --duration-s 8 \
    --interval-ms 1000 \
    --jsonl \
    > "$RESULT_DIR/agent-setgroups.log" 2>&1 &
AGENT_PID="$!"

setgroups_blocked="false"
for _ in $(seq 1 40); do
    if ! kill -0 "$AGENT_PID" 2>/dev/null; then
        set +e
        wait "$AGENT_PID"
        agent_rc="$?"
        set -e
        AGENT_PID=""
        fail "setgroups agent exited before denial was observed, rc=$agent_rc; see $RESULT_DIR/agent-setgroups.log"
    fi

    if ! python3 "$RESULT_DIR/setgroups_transition.py" 65534 \
        > "$RESULT_DIR/setgroups-outside.txt" \
        2> "$RESULT_DIR/setgroups-outside.err"; then
        fail "setgroups transition was denied outside target cgroup; see $RESULT_DIR/setgroups-outside.err"
    fi

    set +e
    run_in_scoped_cgroup "$RESULT_DIR/setgroups-blocked.txt" \
        "$RESULT_DIR/setgroups-blocked.err" \
        python3 "$RESULT_DIR/setgroups_transition.py" 65534
    setgroups_rc="$?"
    set -e
    if [ "$setgroups_rc" -ne 0 ]; then
        setgroups_blocked="true"
        break
    fi
    sleep 0.2
done

if [ "$setgroups_blocked" != "true" ]; then
    fail "setgroups transition was not denied inside target cgroup; see $RESULT_DIR/agent-setgroups.log"
fi

set +e
wait "$AGENT_PID"
agent_rc="$?"
set -e
AGENT_PID=""
if [ "$agent_rc" -ne 0 ]; then
    fail "setgroups agent exited non-zero, rc=$agent_rc; see $RESULT_DIR/agent-setgroups.log"
fi

assert_blocked_rule_event "task_fix_setgroups" "lsm_task_fix_setgroups" \
    "deny_setgroups_transition" "setgroups_scope"
assert_blocked_rule_event_has_cgroup "task_fix_setgroups" "lsm_task_fix_setgroups" \
    "deny_setgroups_transition" "setgroups_scope"
if ! grep -F "task_fix_setgroups" "$ROOT/reports/events/security_policy.jsonl" 2>/dev/null \
    | grep -F '"event_hook":"lsm_task_fix_setgroups"' \
    | grep -F '"result":"blocked"' \
    | grep -F '"group_count":"1"' \
    | grep -Fq '"old_group_count":"'; then
    fail "setgroups blocked event did not carry group_count/old_group_count evidence"
fi
cp "$ROOT/reports/events/security_policy.jsonl" "$RESULT_DIR/security_policy_events.setgroups.jsonl"
log "PASS: security_policy lsm_task_fix_setgroups blocks scoped setgroups transitions"

run_cleanup_script

if ! run_in_scoped_cgroup "$RESULT_DIR/setgroups-post-cleanup.txt" \
    "$RESULT_DIR/setgroups-post-cleanup.err" \
    python3 "$RESULT_DIR/setgroups_transition.py" 65534; then
    fail "setgroups transition is still denied after rollback/cleanup; see $RESULT_DIR/setgroups-post-cleanup.err"
fi
cat > "$RESULT_DIR/cred_prepare_transition.py" <<'PY'
import errno
import os
import sys

target_uid = int(sys.argv[1]) if len(sys.argv) > 1 else 65534
try:
    os.setuid(target_uid)
except OSError as exc:
    print(os.strerror(exc.errno or errno.EPERM), file=sys.stderr)
    sys.exit(1)
sys.exit(0)
PY

if ! python3 "$RESULT_DIR/cred_prepare_transition.py" 65534 \
    > "$RESULT_DIR/cred-prepare-baseline.txt" \
    2> "$RESULT_DIR/cred-prepare-baseline.err"; then
    fail "cred_prepare baseline failed before policy; see $RESULT_DIR/cred-prepare-baseline.err"
fi

cat > "$RESULT_DIR/agent.cred-prepare.yaml" <<'YAML'
skills_config_path: skills.cred-prepare.yaml
exporter:
  prometheus:
    enabled: false
YAML

cat > "$RESULT_DIR/skills.cred-prepare.yaml" <<YAML
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
      cred_prepare_scope:
        type: cgroup
        cgroup_path: $SCOPED_CGROUP_PATH
    rules:
      - name: deny_cred_prepare
        hook: lsm_cred_prepare
        target_ref: cred_prepare_scope
        action: deny
YAML

rm -f "$ROOT/reports/events/security_policy.jsonl"

timeout 20s "$AGENT_BIN" \
    --config "$RESULT_DIR/agent.cred-prepare.yaml" \
    --duration-s 8 \
    --interval-ms 1000 \
    --jsonl \
    > "$RESULT_DIR/agent-cred-prepare.log" 2>&1 &
AGENT_PID="$!"

cred_prepare_blocked="false"
for _ in $(seq 1 40); do
    if ! kill -0 "$AGENT_PID" 2>/dev/null; then
        set +e
        wait "$AGENT_PID"
        agent_rc="$?"
        set -e
        AGENT_PID=""
        fail "cred_prepare agent exited before denial was observed, rc=$agent_rc; see $RESULT_DIR/agent-cred-prepare.log"
    fi

    if ! python3 "$RESULT_DIR/cred_prepare_transition.py" 65534 \
        > "$RESULT_DIR/cred-prepare-outside.txt" \
        2> "$RESULT_DIR/cred-prepare-outside.err"; then
        fail "cred_prepare transition was denied outside target cgroup; see $RESULT_DIR/cred-prepare-outside.err"
    fi

    set +e
    run_in_scoped_cgroup "$RESULT_DIR/cred-prepare-blocked.txt" \
        "$RESULT_DIR/cred-prepare-blocked.err" \
        python3 "$RESULT_DIR/cred_prepare_transition.py" 65534
    cred_prepare_rc="$?"
    set -e
    if [ "$cred_prepare_rc" -ne 0 ]; then
        cred_prepare_blocked="true"
        break
    fi
    sleep 0.2
done

if [ "$cred_prepare_blocked" != "true" ]; then
    fail "cred_prepare transition was not denied inside target cgroup; see $RESULT_DIR/agent-cred-prepare.log"
fi

set +e
wait "$AGENT_PID"
agent_rc="$?"
set -e
AGENT_PID=""
if [ "$agent_rc" -ne 0 ]; then
    fail "cred_prepare agent exited non-zero, rc=$agent_rc; see $RESULT_DIR/agent-cred-prepare.log"
fi

assert_blocked_rule_event "cred_prepare" "lsm_cred_prepare" \
    "deny_cred_prepare" "cred_prepare_scope"
assert_blocked_rule_event_has_cgroup "cred_prepare" "lsm_cred_prepare" \
    "deny_cred_prepare" "cred_prepare_scope"
if ! grep -F "cred_prepare" "$ROOT/reports/events/security_policy.jsonl" 2>/dev/null \
    | grep -F '"event_hook":"lsm_cred_prepare"' \
    | grep -F '"result":"blocked"' \
    | grep -F '"uid":"' \
    | grep -Fq '"cred_gfp":"'; then
    fail "cred_prepare blocked event did not carry uid/cred_gfp evidence"
fi
cp "$ROOT/reports/events/security_policy.jsonl" "$RESULT_DIR/security_policy_events.cred-prepare.jsonl"
log "PASS: security_policy lsm_cred_prepare blocks scoped credential preparation"

run_cleanup_script

if ! run_in_scoped_cgroup "$RESULT_DIR/cred-prepare-post-cleanup.txt" \
    "$RESULT_DIR/cred-prepare-post-cleanup.err" \
    python3 "$RESULT_DIR/cred_prepare_transition.py" 65534; then
    fail "cred_prepare transition is still denied after rollback/cleanup; see $RESULT_DIR/cred-prepare-post-cleanup.err"
fi
sleep 60 &
SCOPED_PID="$!"
echo "$SCOPED_PID" > "$SCOPED_CGROUP_PATH/cgroup.procs"

cat > "$RESULT_DIR/agent.pid.yaml" <<'YAML'
skills_config_path: skills.pid.yaml
exporter:
  prometheus:
    enabled: false
YAML

cat > "$RESULT_DIR/skills.pid.yaml" <<YAML
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
      pid_secret:
        type: pid
        pid: $SCOPED_PID
        path: $DYNAMIC_SCOPED_TARGET_FILE
        exec_prefix: $DYNAMIC_DIR/
    rules:
      - name: deny_pid_secret_open
        hook: lsm_file_open
        target_ref: pid_secret
        action: deny
      - name: deny_pid_exec
        hook: lsm_bprm_check_security
        target_ref: pid_secret
        action: deny
YAML

rm -f "$ROOT/reports/events/security_policy.jsonl"

timeout 20s "$AGENT_BIN" \
    --config "$RESULT_DIR/agent.pid.yaml" \
    --duration-s 8 \
    --interval-ms 1000 \
    --jsonl \
    > "$RESULT_DIR/agent-pid.log" 2>&1 &
AGENT_PID="$!"

pid_blocked="false"
for _ in $(seq 1 40); do
    if ! kill -0 "$AGENT_PID" 2>/dev/null; then
        set +e
        wait "$AGENT_PID"
        agent_rc="$?"
        set -e
        AGENT_PID=""
        fail "pid target agent exited before cgroup denial was observed, rc=$agent_rc; see $RESULT_DIR/agent-pid.log"
    fi

    if ! cat "$DYNAMIC_SCOPED_TARGET_FILE" > "$RESULT_DIR/pid-outside-secret.txt" 2> "$RESULT_DIR/pid-outside-secret.err"; then
        fail "pid target file was denied outside resolved cgroup; see $RESULT_DIR/pid-outside-secret.err"
    fi

    set +e
    run_in_scoped_cgroup "$RESULT_DIR/pid-blocked-secret.txt" \
        "$RESULT_DIR/pid-blocked-secret.err" \
        cat "$DYNAMIC_SCOPED_TARGET_FILE"
    pid_cat_rc="$?"
    set -e
    if [ "$pid_cat_rc" -ne 0 ]; then
        pid_blocked="true"
        break
    fi
    sleep 0.2
done

if [ "$pid_blocked" != "true" ]; then
    fail "pid target file was not denied inside resolved cgroup; see $RESULT_DIR/agent-pid.log"
fi

if ! "$DYNAMIC_SCOPED_EXEC_TARGET_FILE" > "$RESULT_DIR/pid-outside-exec.txt" 2> "$RESULT_DIR/pid-outside-exec.err"; then
    fail "pid exec target was denied outside resolved cgroup; see $RESULT_DIR/pid-outside-exec.err"
fi

set +e
run_in_scoped_cgroup "$RESULT_DIR/pid-blocked-exec.txt" \
    "$RESULT_DIR/pid-blocked-exec.err" \
    "$DYNAMIC_SCOPED_EXEC_TARGET_FILE"
pid_exec_rc="$?"
set -e
if [ "$pid_exec_rc" -eq 0 ]; then
    fail "pid exec target was not denied inside resolved cgroup; see $RESULT_DIR/pid-blocked-exec.txt"
fi

set +e
wait "$AGENT_PID"
agent_rc="$?"
set -e
AGENT_PID=""
if [ "$agent_rc" -ne 0 ]; then
    fail "pid target agent exited non-zero, rc=$agent_rc; see $RESULT_DIR/agent-pid.log"
fi

assert_blocked_rule_event "$DYNAMIC_SCOPED_TARGET_FILE" "lsm_file_open" \
    "deny_pid_secret_open" "pid_secret"
assert_blocked_rule_event "$DYNAMIC_SCOPED_EXEC_TARGET_FILE" "lsm_bprm_check_security" \
    "deny_pid_exec" "pid_secret"
assert_blocked_rule_event_has_cgroup "$DYNAMIC_SCOPED_TARGET_FILE" "lsm_file_open" \
    "deny_pid_secret_open" "pid_secret"
cp "$ROOT/reports/events/security_policy.jsonl" "$RESULT_DIR/security_policy_events.pid.jsonl"
log "PASS: security_policy pid target resolves to cgroup scoped enforcement"

run_cleanup_script

if ! run_in_scoped_cgroup "$RESULT_DIR/pid-post-cleanup-secret.txt" \
    "$RESULT_DIR/pid-post-cleanup-secret.err" \
    cat "$DYNAMIC_SCOPED_TARGET_FILE"; then
    fail "pid target file is still denied after rollback/cleanup; see $RESULT_DIR/pid-post-cleanup-secret.err"
fi
if ! run_in_scoped_cgroup "$RESULT_DIR/pid-post-cleanup-exec.txt" \
    "$RESULT_DIR/pid-post-cleanup-exec.err" \
    "$DYNAMIC_SCOPED_EXEC_TARGET_FILE"; then
    fail "pid exec target is still denied after rollback/cleanup; see $RESULT_DIR/pid-post-cleanup-exec.err"
fi
cleanup_scoped_pid
cleanup_scoped_cgroup

CONTAINER_ID="epcontainer$$abcdef"
SCOPED_CGROUP_PATH="/sys/fs/cgroup/eulerpilot/docker-${CONTAINER_ID}.scope"
mkdir -p /sys/fs/cgroup/eulerpilot
mkdir "$SCOPED_CGROUP_PATH"

cat > "$RESULT_DIR/agent.container.yaml" <<'YAML'
skills_config_path: skills.container.yaml
exporter:
  prometheus:
    enabled: false
YAML

cat > "$RESULT_DIR/skills.container.yaml" <<YAML
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
      container_secret:
        type: container_id
        container_id: $CONTAINER_ID
        cgroup_root: /sys/fs/cgroup/eulerpilot
        path: $DYNAMIC_SCOPED_TARGET_FILE
        exec_prefix: $DYNAMIC_DIR/
    rules:
      - name: deny_container_secret_open
        hook: lsm_file_open
        target_ref: container_secret
        action: deny
      - name: deny_container_exec
        hook: lsm_bprm_check_security
        target_ref: container_secret
        action: deny
YAML

rm -f "$ROOT/reports/events/security_policy.jsonl"

timeout 20s "$AGENT_BIN" \
    --config "$RESULT_DIR/agent.container.yaml" \
    --duration-s 8 \
    --interval-ms 1000 \
    --jsonl \
    > "$RESULT_DIR/agent-container.log" 2>&1 &
AGENT_PID="$!"

container_blocked="false"
for _ in $(seq 1 40); do
    if ! kill -0 "$AGENT_PID" 2>/dev/null; then
        set +e
        wait "$AGENT_PID"
        agent_rc="$?"
        set -e
        AGENT_PID=""
        fail "container target agent exited before cgroup denial was observed, rc=$agent_rc; see $RESULT_DIR/agent-container.log"
    fi

    if ! cat "$DYNAMIC_SCOPED_TARGET_FILE" > "$RESULT_DIR/container-outside-secret.txt" 2> "$RESULT_DIR/container-outside-secret.err"; then
        fail "container target file was denied outside resolved cgroup; see $RESULT_DIR/container-outside-secret.err"
    fi

    set +e
    run_in_scoped_cgroup "$RESULT_DIR/container-blocked-secret.txt" \
        "$RESULT_DIR/container-blocked-secret.err" \
        cat "$DYNAMIC_SCOPED_TARGET_FILE"
    container_cat_rc="$?"
    set -e
    if [ "$container_cat_rc" -ne 0 ]; then
        container_blocked="true"
        break
    fi
    sleep 0.2
done

if [ "$container_blocked" != "true" ]; then
    fail "container target file was not denied inside resolved cgroup; see $RESULT_DIR/agent-container.log"
fi

if ! "$DYNAMIC_SCOPED_EXEC_TARGET_FILE" > "$RESULT_DIR/container-outside-exec.txt" 2> "$RESULT_DIR/container-outside-exec.err"; then
    fail "container exec target was denied outside resolved cgroup; see $RESULT_DIR/container-outside-exec.err"
fi

set +e
run_in_scoped_cgroup "$RESULT_DIR/container-blocked-exec.txt" \
    "$RESULT_DIR/container-blocked-exec.err" \
    "$DYNAMIC_SCOPED_EXEC_TARGET_FILE"
container_exec_rc="$?"
set -e
if [ "$container_exec_rc" -eq 0 ]; then
    fail "container exec target was not denied inside resolved cgroup; see $RESULT_DIR/container-blocked-exec.txt"
fi

set +e
wait "$AGENT_PID"
agent_rc="$?"
set -e
AGENT_PID=""
if [ "$agent_rc" -ne 0 ]; then
    fail "container target agent exited non-zero, rc=$agent_rc; see $RESULT_DIR/agent-container.log"
fi

assert_blocked_rule_event "$DYNAMIC_SCOPED_TARGET_FILE" "lsm_file_open" \
    "deny_container_secret_open" "container_secret"
assert_blocked_rule_event "$DYNAMIC_SCOPED_EXEC_TARGET_FILE" "lsm_bprm_check_security" \
    "deny_container_exec" "container_secret"
assert_blocked_rule_event_has_cgroup "$DYNAMIC_SCOPED_TARGET_FILE" "lsm_file_open" \
    "deny_container_secret_open" "container_secret"
cp "$ROOT/reports/events/security_policy.jsonl" "$RESULT_DIR/security_policy_events.container.jsonl"
log "PASS: security_policy container_id target resolves to cgroup scoped enforcement"

run_cleanup_script

if ! run_in_scoped_cgroup "$RESULT_DIR/container-post-cleanup-secret.txt" \
    "$RESULT_DIR/container-post-cleanup-secret.err" \
    cat "$DYNAMIC_SCOPED_TARGET_FILE"; then
    fail "container target file is still denied after rollback/cleanup; see $RESULT_DIR/container-post-cleanup-secret.err"
fi
if ! run_in_scoped_cgroup "$RESULT_DIR/container-post-cleanup-exec.txt" \
    "$RESULT_DIR/container-post-cleanup-exec.err" \
    "$DYNAMIC_SCOPED_EXEC_TARGET_FILE"; then
    fail "container exec target is still denied after rollback/cleanup; see $RESULT_DIR/container-post-cleanup-exec.err"
fi
cleanup_scoped_cgroup

RUNTIME_CONTAINER_ID="epruntime$$abcdef"
RUNTIME_CONTAINER_NAME="eulerpilot-runtime-$$"
SCOPED_CGROUP_PATH="/sys/fs/cgroup/eulerpilot/cri-containerd-${RUNTIME_CONTAINER_ID}.scope"
FAKE_CRICTL="$DYNAMIC_DIR/fake-crictl"
mkdir -p /sys/fs/cgroup/eulerpilot
mkdir "$SCOPED_CGROUP_PATH"
cat > "$FAKE_CRICTL" <<SH
#!/usr/bin/env bash
if [ "\$1" = "ps" ] && [ "\$2" = "-a" ] && [ "\$3" = "--name" ] && \
   [ "\$4" = "$RUNTIME_CONTAINER_NAME" ]; then
    echo "$RUNTIME_CONTAINER_ID"
    exit 0
fi
exit 1
SH
chmod +x "$FAKE_CRICTL"

cat > "$RESULT_DIR/agent.runtime-container.yaml" <<'YAML'
skills_config_path: skills.runtime-container.yaml
exporter:
  prometheus:
    enabled: false
YAML

cat > "$RESULT_DIR/skills.runtime-container.yaml" <<YAML
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
      runtime_secret:
        type: container
        container_name: $RUNTIME_CONTAINER_NAME
        runtime: crictl
        crictl_path: $FAKE_CRICTL
        cgroup_root: /sys/fs/cgroup/eulerpilot
        path: $DYNAMIC_SCOPED_TARGET_FILE
        exec_prefix: $DYNAMIC_DIR/
    rules:
      - name: deny_runtime_secret_open
        hook: lsm_file_open
        target_ref: runtime_secret
        action: deny
      - name: deny_runtime_exec
        hook: lsm_bprm_check_security
        target_ref: runtime_secret
        action: deny
YAML

rm -f "$ROOT/reports/events/security_policy.jsonl"

timeout 20s "$AGENT_BIN" \
    --config "$RESULT_DIR/agent.runtime-container.yaml" \
    --duration-s 15 \
    --interval-ms 1000 \
    --jsonl \
    > "$RESULT_DIR/agent-runtime-container.log" 2>&1 &
AGENT_PID="$!"

runtime_blocked="false"
for _ in $(seq 1 40); do
    if ! kill -0 "$AGENT_PID" 2>/dev/null; then
        set +e
        wait "$AGENT_PID"
        agent_rc="$?"
        set -e
        AGENT_PID=""
        fail "runtime container target agent exited before cgroup denial was observed, rc=$agent_rc; see $RESULT_DIR/agent-runtime-container.log"
    fi

    if ! cat "$DYNAMIC_SCOPED_TARGET_FILE" > "$RESULT_DIR/runtime-container-outside-secret.txt" 2> "$RESULT_DIR/runtime-container-outside-secret.err"; then
        fail "runtime container target file was denied outside resolved cgroup; see $RESULT_DIR/runtime-container-outside-secret.err"
    fi

    set +e
    run_in_scoped_cgroup "$RESULT_DIR/runtime-container-blocked-secret.txt" \
        "$RESULT_DIR/runtime-container-blocked-secret.err" \
        cat "$DYNAMIC_SCOPED_TARGET_FILE"
    runtime_cat_rc="$?"
    set -e
    if [ "$runtime_cat_rc" -ne 0 ]; then
        runtime_blocked="true"
        break
    fi
    sleep 0.2
done

if [ "$runtime_blocked" != "true" ]; then
    fail "runtime container target file was not denied inside resolved cgroup; see $RESULT_DIR/agent-runtime-container.log"
fi

runtime_exec_blocked="false"
for _ in $(seq 1 40); do
    if ! kill -0 "$AGENT_PID" 2>/dev/null; then
        set +e
        wait "$AGENT_PID"
        agent_rc="$?"
        set -e
        AGENT_PID=""
        fail "runtime container target agent exited before exec denial was checked, rc=$agent_rc; see $RESULT_DIR/agent-runtime-container.log"
    fi

    set +e
    run_in_scoped_cgroup "$RESULT_DIR/runtime-container-blocked-exec.txt" \
        "$RESULT_DIR/runtime-container-blocked-exec.err" \
        "$DYNAMIC_SCOPED_EXEC_TARGET_FILE"
    runtime_exec_rc="$?"
    set -e
    if [ "$runtime_exec_rc" -ne 0 ]; then
        runtime_exec_blocked="true"
        break
    fi
    sleep 0.2
done

if [ "$runtime_exec_blocked" != "true" ]; then
    fail "runtime container exec target was not denied inside resolved cgroup; see $RESULT_DIR/runtime-container-blocked-exec.txt"
fi

set +e
wait "$AGENT_PID"
agent_rc="$?"
set -e
AGENT_PID=""
if [ "$agent_rc" -ne 0 ]; then
    fail "runtime container target agent exited non-zero, rc=$agent_rc; see $RESULT_DIR/agent-runtime-container.log"
fi

assert_blocked_rule_event "$DYNAMIC_SCOPED_TARGET_FILE" "lsm_file_open" \
    "deny_runtime_secret_open" "runtime_secret"
assert_blocked_rule_event "$DYNAMIC_SCOPED_EXEC_TARGET_FILE" "lsm_bprm_check_security" \
    "deny_runtime_exec" "runtime_secret"
assert_blocked_rule_event_has_cgroup "$DYNAMIC_SCOPED_TARGET_FILE" "lsm_file_open" \
    "deny_runtime_secret_open" "runtime_secret"
cp "$ROOT/reports/events/security_policy.jsonl" "$RESULT_DIR/security_policy_events.runtime-container.jsonl"
log "PASS: security_policy container runtime name target resolves to cgroup scoped enforcement"

run_cleanup_script

if ! run_in_scoped_cgroup "$RESULT_DIR/runtime-container-post-cleanup-secret.txt" \
    "$RESULT_DIR/runtime-container-post-cleanup-secret.err" \
    cat "$DYNAMIC_SCOPED_TARGET_FILE"; then
    fail "runtime container target file is still denied after rollback/cleanup; see $RESULT_DIR/runtime-container-post-cleanup-secret.err"
fi
if ! run_in_scoped_cgroup "$RESULT_DIR/runtime-container-post-cleanup-exec.txt" \
    "$RESULT_DIR/runtime-container-post-cleanup-exec.err" \
    "$DYNAMIC_SCOPED_EXEC_TARGET_FILE"; then
    fail "runtime container exec target is still denied after rollback/cleanup; see $RESULT_DIR/runtime-container-post-cleanup-exec.err"
fi
cleanup_scoped_cgroup

POD_UID="12345678-abcd-4ef0-8123-$(printf '%012d' "$$")"
POD_UID_SYSTEMD="${POD_UID//-/_}"
POD_NAME="web-demo"
POD_NAMESPACE="eulerpilot-lab"
SCOPED_CGROUP_PATH="/sys/fs/cgroup/eulerpilot/kubepods-burstable-pod${POD_UID_SYSTEMD}.slice"
FAKE_KUBECTL="$DYNAMIC_DIR/fake-kubectl"
mkdir -p /sys/fs/cgroup/eulerpilot
mkdir "$SCOPED_CGROUP_PATH"
cat > "$FAKE_KUBECTL" <<SH
#!/usr/bin/env bash
if [ "\$1" = "-n" ] && [ "\$2" = "$POD_NAMESPACE" ] && \
   [ "\$3" = "get" ] && [ "\$4" = "pod" ] && [ "\$5" = "$POD_NAME" ]; then
    printf '%s' "$POD_UID"
    exit 0
fi
exit 1
SH
chmod +x "$FAKE_KUBECTL"

cat > "$RESULT_DIR/agent.pod.yaml" <<'YAML'
skills_config_path: skills.pod.yaml
exporter:
  prometheus:
    enabled: false
YAML

cat > "$RESULT_DIR/skills.pod.yaml" <<YAML
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
      pod_secret:
        type: k8s_pod
        namespace: $POD_NAMESPACE
        pod_name: $POD_NAME
        kubectl_path: $FAKE_KUBECTL
        cgroup_root: /sys/fs/cgroup/eulerpilot
        path: $DYNAMIC_SCOPED_TARGET_FILE
        exec_prefix: $DYNAMIC_DIR/
    rules:
      - name: deny_pod_secret_open
        hook: lsm_file_open
        target_ref: pod_secret
        action: deny
      - name: deny_pod_exec
        hook: lsm_bprm_check_security
        target_ref: pod_secret
        action: deny
YAML

rm -f "$ROOT/reports/events/security_policy.jsonl"

timeout 40s "$AGENT_BIN" \
    --config "$RESULT_DIR/agent.pod.yaml" \
    --duration-s 30 \
    --interval-ms 1000 \
    --jsonl \
    > "$RESULT_DIR/agent-pod.log" 2>&1 &
AGENT_PID="$!"

pod_blocked="false"
for _ in $(seq 1 40); do
    if ! kill -0 "$AGENT_PID" 2>/dev/null; then
        set +e
        wait "$AGENT_PID"
        agent_rc="$?"
        set -e
        AGENT_PID=""
        fail "pod target agent exited before cgroup denial was observed, rc=$agent_rc; see $RESULT_DIR/agent-pod.log"
    fi

    if ! cat "$DYNAMIC_SCOPED_TARGET_FILE" > "$RESULT_DIR/pod-outside-secret.txt" 2> "$RESULT_DIR/pod-outside-secret.err"; then
        fail "pod target file was denied outside resolved cgroup; see $RESULT_DIR/pod-outside-secret.err"
    fi

    set +e
    run_in_scoped_cgroup "$RESULT_DIR/pod-blocked-secret.txt" \
        "$RESULT_DIR/pod-blocked-secret.err" \
        cat "$DYNAMIC_SCOPED_TARGET_FILE"
    pod_cat_rc="$?"
    set -e
    if [ "$pod_cat_rc" -ne 0 ]; then
        pod_blocked="true"
        break
    fi
    sleep 0.2
done

if [ "$pod_blocked" != "true" ]; then
    fail "pod target file was not denied inside resolved cgroup; see $RESULT_DIR/agent-pod.log"
fi

pod_exec_blocked="false"
for _ in $(seq 1 40); do
    if ! kill -0 "$AGENT_PID" 2>/dev/null; then
        set +e
        wait "$AGENT_PID"
        agent_rc="$?"
        set -e
        AGENT_PID=""
        fail "pod target agent exited before exec denial was observed, rc=$agent_rc; see $RESULT_DIR/agent-pod.log"
    fi

    set +e
    run_in_scoped_cgroup "$RESULT_DIR/pod-blocked-exec.txt" \
        "$RESULT_DIR/pod-blocked-exec.err" \
        "$DYNAMIC_SCOPED_EXEC_TARGET_FILE"
    pod_exec_rc="$?"
    set -e
    if [ "$pod_exec_rc" -ne 0 ]; then
        pod_exec_blocked="true"
        break
    fi
    sleep 0.2
done

if [ "$pod_exec_blocked" != "true" ]; then
    fail "pod exec target was not denied inside resolved cgroup; see $RESULT_DIR/pod-blocked-exec.txt"
fi

set +e
wait "$AGENT_PID"
agent_rc="$?"
set -e
AGENT_PID=""
if [ "$agent_rc" -ne 0 ]; then
    fail "pod target agent exited non-zero, rc=$agent_rc; see $RESULT_DIR/agent-pod.log"
fi

assert_blocked_rule_event "$DYNAMIC_SCOPED_TARGET_FILE" "lsm_file_open" \
    "deny_pod_secret_open" "pod_secret"
assert_blocked_rule_event "$DYNAMIC_SCOPED_EXEC_TARGET_FILE" "lsm_bprm_check_security" \
    "deny_pod_exec" "pod_secret"
assert_blocked_rule_event_has_cgroup "$DYNAMIC_SCOPED_TARGET_FILE" "lsm_file_open" \
    "deny_pod_secret_open" "pod_secret"
cp "$ROOT/reports/events/security_policy.jsonl" "$RESULT_DIR/security_policy_events.pod.jsonl"
log "PASS: security_policy k8s pod name target resolves to cgroup scoped enforcement"

run_cleanup_script

if ! run_in_scoped_cgroup "$RESULT_DIR/pod-post-cleanup-secret.txt" \
    "$RESULT_DIR/pod-post-cleanup-secret.err" \
    cat "$DYNAMIC_SCOPED_TARGET_FILE"; then
    fail "pod target file is still denied after rollback/cleanup; see $RESULT_DIR/pod-post-cleanup-secret.err"
fi
if ! run_in_scoped_cgroup "$RESULT_DIR/pod-post-cleanup-exec.txt" \
    "$RESULT_DIR/pod-post-cleanup-exec.err" \
    "$DYNAMIC_SCOPED_EXEC_TARGET_FILE"; then
    fail "pod exec target is still denied after rollback/cleanup; see $RESULT_DIR/pod-post-cleanup-exec.err"
fi
cleanup_scoped_cgroup

if bpftool link show 2>/dev/null | grep -q "security_policy_demo"; then
    bpftool link show > "$RESULT_DIR/bpftool-link-after-cleanup.txt" 2>&1 || true
    fail "security_policy_demo BPF link residue found; see $RESULT_DIR/bpftool-link-after-cleanup.txt"
fi
if [ -e /sys/fs/bpf/security_policy_demo ] || [ -e /sys/fs/bpf/security_policy_demo_link ]; then
    fail "security_policy_demo pinned object residue found under /sys/fs/bpf"
fi
log "PASS: cleanup leaves no known security_policy_demo BPF residue"

log "=== SecurityPolicyDemoSkill integration test complete ==="
