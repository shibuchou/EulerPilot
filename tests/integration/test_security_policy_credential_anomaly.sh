#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd -P)"
EXPECTED_ROOT="/root/EulerPilot"
RESULT_DIR="${RESULT_DIR:-$ROOT/results/security_policy/credential-anomaly-$(date +%Y%m%d-%H%M%S)}"
AGENT_BIN="$ROOT/build/eulerpilot-agent"
SCOPED_CGROUP_PATH="/sys/fs/cgroup/eulerpilot/security-credential-churn"
AGENT_PID=""

log() {
    echo "$*" | tee -a "$RESULT_DIR/test.log"
}

fail() {
    log "FAIL: $*"
    exit 1
}

skip() {
    echo "SKIP: $*"
    exit 77
}

require_cmd() {
    local cmd="$1"
    if ! command -v "$cmd" >/dev/null 2>&1; then
        skip "missing command: $cmd"
    fi
}

cleanup_scoped_cgroup() {
    if [ -d "$SCOPED_CGROUP_PATH" ] && [ -f "$SCOPED_CGROUP_PATH/cgroup.procs" ]; then
        while read -r pid; do
            if [ -n "$pid" ]; then
                echo "$pid" > /sys/fs/cgroup/cgroup.procs 2>/dev/null || true
            fi
        done < "$SCOPED_CGROUP_PATH/cgroup.procs"
    fi
    rmdir "$SCOPED_CGROUP_PATH" 2>/dev/null || true
}

cleanup() {
    set +e
    if [ -n "${AGENT_PID:-}" ] && kill -0 "$AGENT_PID" 2>/dev/null; then
        kill "$AGENT_PID" 2>/dev/null || true
        wait "$AGENT_PID" 2>/dev/null || true
    fi
    if [ -f "$ROOT/scripts/cleanup_security_policy_demo.sh" ]; then
        bash "$ROOT/scripts/cleanup_security_policy_demo.sh" >> "$RESULT_DIR/cleanup.log" 2>&1 || true
    fi
    rm -f /sys/fs/bpf/security_policy_demo /sys/fs/bpf/security_policy_demo_link 2>/dev/null || true
    cleanup_scoped_cgroup
}
trap cleanup EXIT

init_scoped_cgroup() {
    mkdir -p /sys/fs/cgroup/eulerpilot
    mkdir -p "$SCOPED_CGROUP_PATH"
    if [ -w "$SCOPED_CGROUP_PATH/cpuset.mems" ]; then
        echo 0 > "$SCOPED_CGROUP_PATH/cpuset.mems" || true
    fi
    if [ -w "$SCOPED_CGROUP_PATH/cpuset.cpus" ]; then
        echo 0-1 > "$SCOPED_CGROUP_PATH/cpuset.cpus" || true
    fi
}

run_in_scoped_cgroup() {
    local out="$1"
    local err="$2"
    shift 2
    bash -c 'echo $$ > "$1/cgroup.procs"; shift; exec "$@"' \
        _ "$SCOPED_CGROUP_PATH" "$@" > "$out" 2> "$err"
}

write_trigger_program() {
    cat > "$RESULT_DIR/credential_transition.py" <<'PY'
import os
import sys

target_uid = int(sys.argv[1])
os.setuid(target_uid)
print(f"uid={os.getuid()} euid={os.geteuid()}")
PY
}

write_config() {
    cat > "$RESULT_DIR/agent.yaml" <<'YAML'
skills_config_path: skills.yaml
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
      credential_scope:
        type: cgroup
        cgroup_path: $SCOPED_CGROUP_PATH
    rules:
      - name: observe_cred_prepare
        hook: lsm_cred_prepare
        target_ref: credential_scope
        action: deny
      - name: observe_setuid_transition
        hook: lsm_task_fix_setuid
        target_ref: credential_scope
        action: deny
    anomaly_rules:
      - name: credential_churn
        type: rate
        syscall: credential
        threshold: 3
        window_ms: 2000
        severity: high
YAML
}

wait_for_security_policy_start() {
    local event_file="$ROOT/reports/events/security_policy.jsonl"
    local deadline=$((SECONDS + 15))
    while [ "$SECONDS" -lt "$deadline" ]; do
        if [ -f "$event_file" ] &&
            grep -F '"operation":"start"' "$event_file" |
                grep -Fq '"skill":"security_policy"'; then
            return 0
        fi
        sleep 0.2
    done
    return 1
}

trigger_credential_churn() {
    for idx in $(seq 1 8); do
        run_in_scoped_cgroup "$RESULT_DIR/credential-$idx.out" \
            "$RESULT_DIR/credential-$idx.err" \
            python3 "$RESULT_DIR/credential_transition.py" 65534
    done
}

wait_for_credential_anomaly() {
    local event_file="$ROOT/reports/events/security_policy.jsonl"
    local deadline=$((SECONDS + 20))
    while [ "$SECONDS" -lt "$deadline" ]; do
        if [ -f "$event_file" ] && python3 - "$event_file" <<'PY'
import json
import sys

for line in open(sys.argv[1], encoding="utf-8", errors="ignore"):
    try:
        item = json.loads(line)
    except json.JSONDecodeError:
        continue
    if item.get("operation") != "anomaly":
        continue
    if item.get("rule_id") != "credential_churn":
        continue
    evidence = item.get("evidence", {})
    if evidence.get("event_hook") not in (
        "lsm_cred_prepare",
        "lsm_task_fix_setuid",
        "lsm_task_fix_setgid",
        "lsm_task_fix_setgroups",
    ):
        continue
    if evidence.get("credential_stage") not in (
        "prepare",
        "setuid",
        "setgid",
        "setgroups",
    ):
        continue
    if "uid" not in evidence and "cred_gfp" not in evidence:
        continue
    raise SystemExit(0)
raise SystemExit(1)
PY
        then
            return 0
        fi
        sleep 0.2
    done
    return 1
}

copy_credential_events() {
    local event_file="$ROOT/reports/events/security_policy.jsonl"
    local anomaly_out="$RESULT_DIR/security_policy_events.credential-anomaly.jsonl"
    local hits_out="$RESULT_DIR/security_policy_events.credential-hits.jsonl"
    if [ ! -f "$event_file" ]; then
        : > "$anomaly_out"
        : > "$hits_out"
        return 0
    fi
    python3 - "$event_file" "$anomaly_out" "$hits_out" <<'PY'
import json
import sys

src, anomaly_dst, hits_dst = sys.argv[1], sys.argv[2], sys.argv[3]
credential_hooks = {
    "lsm_cred_prepare",
    "lsm_task_fix_setuid",
    "lsm_task_fix_setgid",
    "lsm_task_fix_setgroups",
}
with open(src, encoding="utf-8", errors="ignore") as inp, \
        open(anomaly_dst, "w", encoding="utf-8") as anomaly_out, \
        open(hits_dst, "w", encoding="utf-8") as hits_out:
    for line in inp:
        try:
            item = json.loads(line)
        except json.JSONDecodeError:
            continue
        evidence = item.get("evidence", {})
        hook = evidence.get("event_hook")
        if item.get("operation") == "anomaly" and item.get("rule_id") == "credential_churn":
            print(json.dumps(item, ensure_ascii=False, separators=(",", ":")), file=anomaly_out)
        if hook in credential_hooks:
            print(json.dumps(item, ensure_ascii=False, separators=(",", ":")), file=hits_out)
PY
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
        printf 'rule=credential_churn\n'
        printf 'trigger_hooks=lsm_cred_prepare,lsm_task_fix_setuid\n'
        printf 'cgroup_path=%s\n' "$SCOPED_CGROUP_PATH"
        printf 'event_file=security_policy_events.credential-anomaly.jsonl\n'
    } > "$RESULT_DIR/summary.txt"
}

write_report() {
    local result reason
    result="$(awk -F= '$1=="result"{print $2}' "$RESULT_DIR/summary.txt" 2>/dev/null || true)"
    reason="$(awk -F= '$1=="reason"{print $2}' "$RESULT_DIR/summary.txt" 2>/dev/null || true)"
    cat > "$RESULT_DIR/report.md" <<EOF_REPORT
# Security Policy Credential Anomaly

- result: \`${result:-unknown}\`
- reason: \`${reason:-unknown}\`
- host: \`$(hostname 2>/dev/null || printf unknown)\`
- kernel: \`$(uname -r)\`
- rule: \`credential_churn\`

## Purpose

This test validates the Security Policy credential lifecycle anomaly rule. It
runs the Agent in audit mode, scopes \`lsm_cred_prepare\` and
\`lsm_task_fix_setuid\` to a lab cgroup, triggers repeated credential
transitions with \`setuid(65534)\`, and checks that a \`credential_churn\`
anomaly includes lifecycle evidence such as \`credential_stage\` plus \`uid\`
or \`cred_gfp\`, depending on the hook that crosses the anomaly threshold.

## Artifacts

- \`summary.txt\`
- \`report.md\`
- \`agent.yaml\`, \`skills.yaml\`
- \`agent.log\`
- \`security_policy_events.credential-anomaly.jsonl\`
- \`security_policy_events.credential-hits.jsonl\`
- \`credential-*.out\`, \`credential-*.err\`
EOF_REPORT
}

if [ "$ROOT" != "$EXPECTED_ROOT" ]; then
    skip "current root is $ROOT; Agent BPF object path currently expects $EXPECTED_ROOT"
fi
if [ "$(id -u)" -ne 0 ]; then
    skip "BPF LSM attach requires root or equivalent capabilities"
fi

mkdir -p "$RESULT_DIR"
: > "$RESULT_DIR/test.log"
log "=== SecurityPolicy credential anomaly integration test ==="

require_cmd make
require_cmd timeout
require_cmd grep
require_cmd python3

write_trigger_program
if ! python3 "$RESULT_DIR/credential_transition.py" 65534 \
    > "$RESULT_DIR/credential-baseline.out" \
    2> "$RESULT_DIR/credential-baseline.err"; then
    skip "baseline setuid failed; see $RESULT_DIR/credential-baseline.err"
fi

make agent security-policy-demo >> "$RESULT_DIR/build.log" 2>&1
[ -x "$AGENT_BIN" ] || fail "missing $AGENT_BIN"
[ -f "$ROOT/build/security_policy_demo.bpf.o" ] || fail "missing security_policy_demo.bpf.o"

init_scoped_cgroup
write_config
rm -f "$ROOT/reports/events/security_policy.jsonl"

timeout 24s "$AGENT_BIN" \
    --config "$RESULT_DIR/agent.yaml" \
    --duration-s 10 \
    --interval-ms 1000 \
    --jsonl \
    > "$RESULT_DIR/agent.log" 2>&1 &
AGENT_PID="$!"

if ! kill -0 "$AGENT_PID" 2>/dev/null; then
    set +e
    wait "$AGENT_PID"
    agent_rc="$?"
    set -e
    AGENT_PID=""
    fail "credential anomaly agent exited early, rc=$agent_rc; see $RESULT_DIR/agent.log"
fi
if ! wait_for_security_policy_start; then
    fail "security_policy did not report start before trigger phase; see $RESULT_DIR/agent.log"
fi

trigger_credential_churn

if ! wait_for_credential_anomaly; then
    copy_credential_events
    fail "credential_churn anomaly was not observed; see $RESULT_DIR/agent.log"
fi

set +e
wait "$AGENT_PID"
agent_rc="$?"
set -e
AGENT_PID=""
if [ "$agent_rc" -ne 0 ]; then
    fail "credential anomaly agent exited non-zero, rc=$agent_rc; see $RESULT_DIR/agent.log"
fi

copy_credential_events

python3 - "$RESULT_DIR/security_policy_events.credential-anomaly.jsonl" \
    > "$RESULT_DIR/anomaly_event_summary.txt" <<'PY'
import json
import sys

for line in open(sys.argv[1], encoding="utf-8", errors="ignore"):
    try:
        item = json.loads(line)
    except json.JSONDecodeError:
        continue
    evidence = item.get("evidence", {})
    print(
        f"{item.get('rule_id')} "
        f"hook={evidence.get('event_hook')} "
        f"stage={evidence.get('credential_stage')} "
        f"threshold={evidence.get('threshold')} "
        f"window_ms={evidence.get('window_ms')} "
        f"hit_count={evidence.get('hit_count')} "
        f"uid={evidence.get('uid')} "
        f"cred_gfp={evidence.get('cred_gfp')}"
    )
PY

write_summary "pass" "credential-churn-anomaly-observed"
write_report
log "PASS: security_policy observes credential_churn anomaly with lifecycle evidence"
