#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd -P)"
EXPECTED_ROOT="/root/EulerPilot"
RESULT_DIR="${RESULT_DIR:-$ROOT/results/security_policy/anomaly-process-filter-$(date +%Y%m%d-%H%M%S)}"
AGENT_BIN="$ROOT/build/eulerpilot-agent"
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
}
trap cleanup EXIT

write_config() {
    cat > "$RESULT_DIR/agent.yaml" <<'YAML'
skills_config_path: skills.yaml
exporter:
  prometheus:
    enabled: false
YAML

    cat > "$RESULT_DIR/skills.yaml" <<'YAML'
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
      sensitive_file:
        type: path
        path: /etc/hostname
    rules:
      - name: observe_sensitive_file
        hook: lsm_file_open
        target_ref: sensitive_file
        action: deny
    anomaly_rules:
      - name: burst_openat_python_prefix
        type: rate
        syscall: openat
        path_prefix: /etc
        comm_prefix: python
        threshold: 3
        window_ms: 2000
        severity: medium
      - name: burst_openat_nohit_comm
        type: rate
        syscall: openat
        path_prefix: /etc
        comm: nohit-proc
        threshold: 3
        window_ms: 2000
        severity: medium
YAML
}

write_trigger_program() {
    cat > "$RESULT_DIR/openat_trigger.py" <<'PY'
for _ in range(10):
    with open("/etc/hostname", "rb") as fp:
        fp.read(1)
PY
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
    for idx in $(seq 1 4); do
        python3 "$RESULT_DIR/openat_trigger.py" \
            > "$RESULT_DIR/openat-python-$idx.out" \
            2> "$RESULT_DIR/openat-python-$idx.err"
    done
}

copy_anomaly_events() {
    local event_file="$ROOT/reports/events/security_policy.jsonl"
    local out_file="$RESULT_DIR/security_policy_events.anomaly-process-filter.jsonl"
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

wait_for_filtered_anomaly() {
    local event_file="$ROOT/reports/events/security_policy.jsonl"
    local deadline=$((SECONDS + 20))
    while [ "$SECONDS" -lt "$deadline" ]; do
        if [ -f "$event_file" ] && python3 - "$event_file" <<'PY'
import json
import sys

matched = False
nohit = False
for line in open(sys.argv[1], encoding="utf-8", errors="ignore"):
    try:
        item = json.loads(line)
    except json.JSONDecodeError:
        continue
    if item.get("operation") != "anomaly":
        continue
    evidence = item.get("evidence", {})
    if item.get("rule_id") == "burst_openat_python_prefix":
        if (evidence.get("event_hook") in ("sys_enter_openat", "lsm_file_open") and
                evidence.get("path_prefix") == "/etc" and
                evidence.get("comm", "").startswith("python") and
                evidence.get("comm_prefix") == "python"):
            matched = True
    if item.get("rule_id") == "burst_openat_nohit_comm":
        nohit = True
if not matched or nohit:
    raise SystemExit(1)
PY
        then
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
        printf 'positive_rule=burst_openat_python_prefix\n'
        printf 'positive_comm_prefix=python\n'
        printf 'negative_rule=burst_openat_nohit_comm\n'
        printf 'negative_comm=nohit-proc\n'
        printf 'event_file=security_policy_events.anomaly-process-filter.jsonl\n'
    } > "$RESULT_DIR/summary.txt"
}

write_report() {
    local result reason
    result="$(awk -F= '$1=="result"{print $2}' "$RESULT_DIR/summary.txt" 2>/dev/null || true)"
    reason="$(awk -F= '$1=="reason"{print $2}' "$RESULT_DIR/summary.txt" 2>/dev/null || true)"
    cat > "$RESULT_DIR/report.md" <<EOF_REPORT
# Security Policy Anomaly Process Filter

- result: \`${result:-unknown}\`
- reason: \`${reason:-unknown}\`
- host: \`$(hostname 2>/dev/null || printf unknown)\`
- kernel: \`$(uname -r)\`
- positive rule: \`burst_openat_python_prefix\`
- negative rule: \`burst_openat_nohit_comm\`

## Purpose

This test validates user-space process filtering for Security anomaly rules.
The same \`/etc\` openat burst is allowed to trigger only when the event comm
matches \`comm_prefix=python\`. The negative rule uses \`comm=nohit-proc\` and
must not emit an anomaly.

## Artifacts

- \`summary.txt\`
- \`report.md\`
- \`agent.yaml\`, \`skills.yaml\`
- \`agent.log\`
- \`security_policy_events.anomaly-process-filter.jsonl\`
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
log "=== SecurityPolicy anomaly process filter integration test ==="

require_cmd make
require_cmd timeout
require_cmd grep
require_cmd python3

if [ ! -x "$AGENT_BIN" ] || [ ! -f "$ROOT/build/security_policy.bpf.o" ]; then
    make agent security-policy >> "$RESULT_DIR/build.log" 2>&1
fi
[ -x "$AGENT_BIN" ] || fail "missing $AGENT_BIN"
[ -f "$ROOT/build/security_policy.bpf.o" ] || fail "missing security_policy.bpf.o"

write_config
write_trigger_program
rm -f "$ROOT/reports/events/security_policy.jsonl"

timeout 20s "$AGENT_BIN" \
    --config "$RESULT_DIR/agent.yaml" \
    --duration-s 8 \
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
    fail "process-filter agent exited early, rc=$agent_rc; see $RESULT_DIR/agent.log"
fi
if ! wait_for_security_policy_start; then
    fail "security_policy did not report start before trigger phase; see $RESULT_DIR/agent.log"
fi

trigger_python_openat_burst

if ! wait_for_filtered_anomaly; then
    copy_anomaly_events
    fail "filtered openat anomaly was not observed or negative comm filter fired; see $RESULT_DIR/agent.log"
fi

set +e
wait "$AGENT_PID"
agent_rc="$?"
set -e
AGENT_PID=""
if [ "$agent_rc" -ne 0 ]; then
    fail "process-filter agent exited non-zero, rc=$agent_rc; see $RESULT_DIR/agent.log"
fi

copy_anomaly_events
python3 - "$RESULT_DIR/security_policy_events.anomaly-process-filter.jsonl" \
    > "$RESULT_DIR/anomaly_process_filter_summary.txt" <<'PY'
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
        f"comm={evidence.get('comm')} "
        f"comm_prefix={evidence.get('comm_prefix')} "
        f"hit_count={evidence.get('hit_count')}"
    )
PY

if grep -Fq '"rule_id":"burst_openat_nohit_comm"' \
    "$RESULT_DIR/security_policy_events.anomaly-process-filter.jsonl"; then
    fail "negative comm filter emitted anomaly"
fi

write_summary "pass" "security-anomaly-process-filter-observed"
write_report
log "PASS: security_policy anomaly rules support process comm filtering"
