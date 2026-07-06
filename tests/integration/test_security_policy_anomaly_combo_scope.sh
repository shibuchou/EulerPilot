#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd -P)"
EXPECTED_ROOT="/root/EulerPilot"
RESULT_DIR="${RESULT_DIR:-$ROOT/results/security_policy/anomaly-combo-scope-$(date +%Y%m%d-%H%M%S)}"
AGENT_BIN="$ROOT/build/eulerpilot-agent"
SCOPED_CGROUP_PATH="/sys/fs/cgroup/eulerpilot/security-anomaly-combo-scope"
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
    cat > "$RESULT_DIR/openat_trigger.py" <<'PY'
for _ in range(10):
    with open("/etc/hostname", "rb") as fp:
        fp.read(1)
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
      scoped_python_etc:
        type: path
        path_prefix: /etc
        cgroup_path: $SCOPED_CGROUP_PATH
    rules:
      - name: observe_scoped_python_etc
        hook: lsm_file_open
        target_ref: scoped_python_etc
        action: deny
    anomaly_rules:
      - name: burst_openat_python_scoped_etc
        type: rate
        syscall: openat
        target_ref: scoped_python_etc
        path_prefix: /etc
        comm_prefix: python
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

trigger_python_openat_burst() {
    local phase="$1"
    for idx in $(seq 1 4); do
        python3 "$RESULT_DIR/openat_trigger.py" \
            > "$RESULT_DIR/openat-$phase-$idx.out" \
            2> "$RESULT_DIR/openat-$phase-$idx.err"
    done
}

trigger_scoped_python_openat_burst() {
    for idx in $(seq 1 4); do
        run_in_scoped_cgroup "$RESULT_DIR/openat-scoped-$idx.out" \
            "$RESULT_DIR/openat-scoped-$idx.err" \
            python3 "$RESULT_DIR/openat_trigger.py"
    done
}

copy_anomaly_events() {
    local event_file="$ROOT/reports/events/security_policy.jsonl"
    local out_file="$RESULT_DIR/security_policy_events.anomaly-combo-scope.jsonl"
    if [ ! -f "$event_file" ]; then
        : > "$out_file"
        return 0
    fi
    python3 - "$event_file" "$out_file" <<'PY'
import json
import sys

src, dst = sys.argv[1], sys.argv[2]
with open(src, encoding="utf-8", errors="ignore") as inp, \
        open(dst, "w", encoding="utf-8") as out:
    for line in inp:
        try:
            item = json.loads(line)
        except json.JSONDecodeError:
            continue
        if item.get("operation") == "anomaly":
            print(json.dumps(item, ensure_ascii=False, separators=(",", ":")), file=out)
PY
}

combo_anomaly_seen() {
    local event_file="$ROOT/reports/events/security_policy.jsonl"
    [ -f "$event_file" ] || return 1
    python3 - "$event_file" <<'PY'
import json
import sys

for line in open(sys.argv[1], encoding="utf-8", errors="ignore"):
    try:
        item = json.loads(line)
    except json.JSONDecodeError:
        continue
    if item.get("operation") != "anomaly":
        continue
    if item.get("rule_id") != "burst_openat_python_scoped_etc":
        continue
    evidence = item.get("evidence", {})
    target = item.get("target", {})
    if evidence.get("event_hook") != "lsm_file_open":
        continue
    if evidence.get("path_prefix") != "/etc":
        continue
    if not evidence.get("comm", "").startswith("python"):
        continue
    if evidence.get("comm_prefix") != "python":
        continue
    if evidence.get("target_ref_filter") != "scoped_python_etc":
        continue
    if target.get("target_ref") != "scoped_python_etc":
        continue
    if not target.get("cgroup_id") or not target.get("cgroup_path"):
        continue
    raise SystemExit(0)
raise SystemExit(1)
PY
}

wait_for_combo_anomaly() {
    local deadline=$((SECONDS + 20))
    while [ "$SECONDS" -lt "$deadline" ]; do
        if combo_anomaly_seen; then
            return 0
        fi
        sleep 0.2
    done
    return 1
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
        printf 'rule=burst_openat_python_scoped_etc\n'
        printf 'target_ref=scoped_python_etc\n'
        printf 'cgroup_path=%s\n' "$SCOPED_CGROUP_PATH"
        printf 'filters=syscall=openat,path_prefix=/etc,comm_prefix=python,target_ref=scoped_python_etc\n'
        printf 'event_file=security_policy_events.anomaly-combo-scope.jsonl\n'
    } > "$RESULT_DIR/summary.txt"
}

write_report() {
    local result reason
    result="$(awk -F= '$1=="result"{print $2}' "$RESULT_DIR/summary.txt" 2>/dev/null || true)"
    reason="$(awk -F= '$1=="reason"{print $2}' "$RESULT_DIR/summary.txt" 2>/dev/null || true)"
    cat > "$RESULT_DIR/report.md" <<EOF_REPORT
# Security Policy Anomaly Combo Scope

- result: \`${result:-unknown}\`
- reason: \`${reason:-unknown}\`
- host: \`$(hostname 2>/dev/null || printf unknown)\`
- kernel: \`$(uname -r)\`
- rule: \`burst_openat_python_scoped_etc\`

## Purpose

This test validates multi-dimensional anomaly filtering. The rule requires the
same event to match \`syscall=openat\`, \`path_prefix=/etc\`,
\`comm_prefix=python\`, and \`target_ref=scoped_python_etc\`. The test first
runs the same Python \`/etc\` open burst outside the cgroup and verifies that no
anomaly is emitted. It then runs the burst inside the scoped cgroup and verifies
that the emitted anomaly carries target scope evidence.

## Artifacts

- \`summary.txt\`
- \`report.md\`
- \`agent.yaml\`, \`skills.yaml\`
- \`agent.log\`
- \`security_policy_events.anomaly-combo-scope.jsonl\`
- \`anomaly_combo_scope_summary.txt\`
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
log "=== SecurityPolicy anomaly combo scope integration test ==="

require_cmd make
require_cmd timeout
require_cmd grep
require_cmd python3

if [ ! -x "$AGENT_BIN" ] || [ ! -f "$ROOT/build/security_policy.bpf.o" ]; then
    make agent security-policy >> "$RESULT_DIR/build.log" 2>&1
fi
[ -x "$AGENT_BIN" ] || fail "missing $AGENT_BIN"
[ -f "$ROOT/build/security_policy.bpf.o" ] || fail "missing security_policy.bpf.o"

init_scoped_cgroup
write_config
write_trigger_program
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
    fail "combo-scope agent exited early, rc=$agent_rc; see $RESULT_DIR/agent.log"
fi
if ! wait_for_security_policy_start; then
    fail "security_policy did not report start before trigger phase; see $RESULT_DIR/agent.log"
fi

trigger_python_openat_burst "outside"
sleep 1
if combo_anomaly_seen; then
    copy_anomaly_events
    fail "combo-scope anomaly fired for non-target cgroup"
fi

trigger_scoped_python_openat_burst

if ! wait_for_combo_anomaly; then
    copy_anomaly_events
    fail "scoped combo anomaly was not observed; see $RESULT_DIR/agent.log"
fi

set +e
wait "$AGENT_PID"
agent_rc="$?"
set -e
AGENT_PID=""
if [ "$agent_rc" -ne 0 ]; then
    fail "combo-scope agent exited non-zero, rc=$agent_rc; see $RESULT_DIR/agent.log"
fi

copy_anomaly_events
python3 - "$RESULT_DIR/security_policy_events.anomaly-combo-scope.jsonl" \
    > "$RESULT_DIR/anomaly_combo_scope_summary.txt" <<'PY'
import json
import sys

for line in open(sys.argv[1], encoding="utf-8", errors="ignore"):
    try:
        item = json.loads(line)
    except json.JSONDecodeError:
        continue
    evidence = item.get("evidence", {})
    target = item.get("target", {})
    print(
        f"{item.get('rule_id')} "
        f"hook={evidence.get('event_hook')} "
        f"comm={evidence.get('comm')} "
        f"path_prefix={evidence.get('path_prefix')} "
        f"target_ref_filter={evidence.get('target_ref_filter')} "
        f"target_ref={target.get('target_ref')} "
        f"cgroup_id={target.get('cgroup_id')} "
        f"hit_count={evidence.get('hit_count')}"
    )
PY

write_summary "pass" "security-anomaly-combo-scope-observed"
write_report
log "PASS: security_policy anomaly rules support target_ref + path + process filtering"
