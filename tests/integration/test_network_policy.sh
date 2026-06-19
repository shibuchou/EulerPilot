#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

AGENT_BIN="build/eulerpilot-agent"
AGENT_YAML="configs/agent.yaml"
SKILLS_YAML="configs/skills.yaml"
RESULT_DIR="results/network_policy/integration-$(date +%Y%m%d-%H%M%S)"
DEMO_CGROUP="/sys/fs/cgroup/eulerpilot/demo-net"
HTTP_PORT="18081"
AGENT_PID=""
SERVER_PID=""
mkdir -p "$RESULT_DIR"

log() {
    echo "$*" | tee -a "$RESULT_DIR/test.log"
}

restore() {
    if [ -n "${AGENT_PID:-}" ] && kill -0 "$AGENT_PID" 2>/dev/null; then
        kill "$AGENT_PID" 2>/dev/null || true
        wait "$AGENT_PID" 2>/dev/null || true
    fi
    if [ -n "${SERVER_PID:-}" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    if [ -f "$RESULT_DIR/skills.yaml.bak" ]; then
        cp "$RESULT_DIR/skills.yaml.bak" "$SKILLS_YAML"
    fi
    bash scripts/cleanup_network_policy_demo.sh >/dev/null 2>&1 || true
}
trap restore EXIT

configure_network_policy() {
    local mode="$1"
    local port="$2"

    NETWORK_POLICY_MODE="$mode" NETWORK_POLICY_PORT="$port" python3 - <<'PY'
import os
from pathlib import Path

path = Path("configs/skills.yaml")
lines = path.read_text().splitlines()
mode = os.environ["NETWORK_POLICY_MODE"]
port = os.environ["NETWORK_POLICY_PORT"]

prefix = []
blocks = []
current = []
in_skills = False
for line in lines:
    if line.startswith("skills:"):
        in_skills = True
        prefix.append(line)
        continue
    if in_skills and line.startswith("- "):
        if current:
            blocks.append(current)
        current = [line]
        continue
    if in_skills:
        current.append(line)
    else:
        prefix.append(line)
if current:
    blocks.append(current)

seen = False
out = prefix[:]
for block in blocks:
    name = ""
    for item in block:
        stripped = item.strip()
        if stripped.startswith("- name:"):
            name = stripped.split(":", 1)[1].strip().strip("'\"")
            break
        if stripped.startswith("name:"):
            name = stripped.split(":", 1)[1].strip().strip("'\"")
            break

    if name == "network_policy":
        seen = True

    new_block = []
    for item in block:
        stripped = item.strip()
        indent = item[:len(item) - len(item.lstrip())]
        if stripped.startswith("enabled:"):
            if name == "network_policy":
                enabled = "true"
            elif name in ("network_policy_demo", "security_policy_demo"):
                enabled = "false"
            else:
                enabled = stripped.split(":", 1)[1].strip()
            new_block.append(f"{indent}enabled: {enabled}")
        elif name == "network_policy" and stripped.startswith("mode:"):
            new_block.append(f"{indent}mode: {mode}")
        elif name == "network_policy" and stripped.startswith("dst_port:"):
            new_block.append(f"{indent}dst_port: '{port}'")
        else:
            new_block.append(item)
    out.extend(new_block)

if not seen:
    raise SystemExit("network_policy missing from skills.yaml")
path.write_text("\n".join(out) + "\n")
PY
}

log "=== NetworkPolicySkill integration test ==="
cp "$SKILLS_YAML" "$RESULT_DIR/skills.yaml.bak"

if ! "$AGENT_BIN" --list-skills | grep -qx "network_policy"; then
    log "FAIL: formal network_policy skill is not registered"
    exit 1
fi
log "PASS: formal network_policy skill is registered"

configure_network_policy audit "$HTTP_PORT"
cp "$SKILLS_YAML" "$RESULT_DIR/skills.audit.yaml"

timeout 20s "$AGENT_BIN" --doctor-skills --config "$AGENT_YAML" > "$RESULT_DIR/doctor.log" 2>&1
log "PASS: doctor succeeds with network_policy audit mode"

timeout 20s "$AGENT_BIN" --config "$AGENT_YAML" --duration-s 1 --interval-ms 1000 > "$RESULT_DIR/agent.log" 2>&1
log "PASS: agent runs with network_policy audit mode"

if [ -d /sys/fs/cgroup/eulerpilot/demo-net ] &&
   bpftool cgroup show /sys/fs/cgroup/eulerpilot/demo-net 2>/dev/null | grep -q .; then
    log "FAIL: audit mode attached cgroup BPF unexpectedly"
    exit 1
fi
log "PASS: audit mode has no cgroup BPF attachment"

if [ ! -s reports/events/network_policy.jsonl ]; then
    log "FAIL: expected network policy audit event"
    exit 1
fi
log "PASS: network policy audit event written"

configure_network_policy enforce "$HTTP_PORT"
cp "$SKILLS_YAML" "$RESULT_DIR/skills.enforce.yaml"
timeout 20s "$AGENT_BIN" --doctor-skills --config "$AGENT_YAML" > "$RESULT_DIR/enforce-doctor.log" 2>&1
log "PASS: doctor succeeds with network_policy enforce mode"

python3 -m http.server "$HTTP_PORT" --bind 127.0.0.1 --directory "$RESULT_DIR" \
    > "$RESULT_DIR/http-server.log" 2>&1 &
SERVER_PID="$!"
sleep 1

curl -fsS --connect-timeout 1 --max-time 2 "http://127.0.0.1:$HTTP_PORT/" \
    > "$RESULT_DIR/outside-cgroup-curl.html"
log "PASS: outside-cgroup curl succeeds before policy hit"

timeout 25s "$AGENT_BIN" --config "$AGENT_YAML" --duration-s 8 --interval-ms 1000 \
    > "$RESULT_DIR/enforce-agent.log" 2>&1 &
AGENT_PID="$!"

attached="false"
for _ in $(seq 1 30); do
    if ! kill -0 "$AGENT_PID" 2>/dev/null; then
        wait "$AGENT_PID" 2>/dev/null || true
        AGENT_PID=""
        log "FAIL: enforce agent exited before cgroup/connect4 attach"
        exit 1
    fi
    pin_ready="false"
    cgroup_ready="false"
    if [ -e /sys/fs/bpf/eulerpilot_network_policy_link ]; then
        pin_ready="true"
    fi
    if [ -d "$DEMO_CGROUP" ] &&
       bpftool cgroup show "$DEMO_CGROUP" 2>/dev/null | grep -Eq "connect|inet4"; then
        cgroup_ready="true"
    fi
    if [ "$pin_ready" = "true" ] || [ "$cgroup_ready" = "true" ]; then
        attached="true"
        break
    fi
    sleep 0.2
done
if [ "$attached" != "true" ]; then
    {
        echo "agent_timeout_pid=$AGENT_PID"
        ps -ef | grep -E "eulerpilot-agent|timeout 25s" | grep -v grep || true
        ps -o pid,ppid,stat,cmd -p "$AGENT_PID" 2>/dev/null || true
        pgrep -a -P "$AGENT_PID" 2>/dev/null || true
        echo "--- cgroup ---"
        ls -ld "$DEMO_CGROUP" 2>/dev/null || true
        bpftool cgroup show "$DEMO_CGROUP" 2>&1 || true
        echo "--- pins ---"
        ls -l /sys/fs/bpf/eulerpilot_network_policy_link \
              /sys/fs/bpf/eulerpilot_network_policy_demo_link 2>/dev/null || true
        echo "--- agent log ---"
        cat "$RESULT_DIR/enforce-agent.log" 2>/dev/null || true
    } > "$RESULT_DIR/enforce-attach-debug.log"
    log "FAIL: enforce mode did not attach cgroup/connect4 program"
    exit 1
fi
log "PASS: enforce mode attaches cgroup/connect4 program"

set +e
blocked_code=$(bash -c "echo \$\$ > '$DEMO_CGROUP/cgroup.procs'; curl -sS -o /dev/null -w '%{http_code}' --connect-timeout 1 --max-time 2 'http://127.0.0.1:$HTTP_PORT/'" \
    2> "$RESULT_DIR/enforce-curl.err")
blocked_rc="$?"
set -e
printf 'rc=%s http_code=%s\n' "$blocked_rc" "$blocked_code" > "$RESULT_DIR/enforce-curl.result"
if [ "$blocked_rc" -eq 0 ]; then
    log "FAIL: target-cgroup curl unexpectedly succeeded"
    exit 1
fi
log "PASS: target-cgroup curl is denied by dynamic dst_port=$HTTP_PORT"

wait "$AGENT_PID"
AGENT_PID=""
log "PASS: enforce agent exits cleanly"

if [ -e /sys/fs/bpf/eulerpilot_network_policy_link ] ||
   { [ -d "$DEMO_CGROUP" ] && bpftool cgroup show "$DEMO_CGROUP" 2>/dev/null | grep -q "connect4"; }; then
    log "FAIL: rollback left network policy attachment residue"
    exit 1
fi
log "PASS: rollback leaves no network policy BPF attachment"

log "=== NetworkPolicySkill integration test complete ==="
