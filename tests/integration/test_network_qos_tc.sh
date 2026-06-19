#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

AGENT_BIN="build/eulerpilot-agent"
AGENT_YAML="configs/agent.yaml"
SKILLS_YAML="configs/skills.yaml"
QOS_IFACE="ep-veth-qos0"
QOS_PEER="ep-veth-qos1"
QOS_NETNS="ep-qos-ns"
QOS_ADDR="10.88.0.1/24"
QOS_PEER_ADDR="10.88.0.2/24"
QOS_PEER_IP="10.88.0.2"
RESULT_DIR="results/network_policy/qos-tc-$(date +%Y%m%d-%H%M%S)"
AGENT_PID=""
mkdir -p "$RESULT_DIR"

log() {
    echo "$*" | tee -a "$RESULT_DIR/test.log"
}

restore() {
    if [ -n "${AGENT_PID:-}" ] && kill -0 "$AGENT_PID" 2>/dev/null; then
        kill "$AGENT_PID" 2>/dev/null || true
        wait "$AGENT_PID" 2>/dev/null || true
    fi
    if [ -f "$RESULT_DIR/skills.yaml.bak" ]; then
        cp "$RESULT_DIR/skills.yaml.bak" "$SKILLS_YAML"
    fi
    bash scripts/cleanup_network_qos_tc.sh "$QOS_IFACE" "$QOS_PEER" "$QOS_NETNS" >/dev/null 2>&1 || true
}
trap restore EXIT

configure_network_qos() {
    local mode="$1"

    NETWORK_QOS_MODE="$mode" \
    NETWORK_QOS_IFACE="$QOS_IFACE" \
    python3 - <<'PY'
import os
from pathlib import Path

path = Path("configs/skills.yaml")
lines = path.read_text().splitlines()
mode = os.environ["NETWORK_QOS_MODE"]
ifname = os.environ["NETWORK_QOS_IFACE"]

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

    if name == "network_qos":
        seen = True

    new_block = []
    for item in block:
        stripped = item.strip()
        indent = item[:len(item) - len(item.lstrip())]
        if stripped.startswith("enabled:"):
            if name == "network_qos":
                enabled = "true"
            elif name in ("network_policy", "network_policy_demo", "security_policy_demo"):
                enabled = "false"
            else:
                enabled = stripped.split(":", 1)[1].strip()
            new_block.append(f"{indent}enabled: {enabled}")
        elif name == "network_qos" and stripped.startswith("mode:"):
            new_block.append(f"{indent}mode: {mode}")
        elif name == "network_qos" and stripped.startswith("ifname:"):
            new_block.append(f"{indent}ifname: {ifname}")
        elif name == "network_qos" and stripped.startswith("protocol:"):
            new_block.append(f"{indent}protocol: any")
        elif name == "network_qos" and stripped.startswith("dst_port:"):
            new_block.append(f"{indent}dst_port: '0'")
        elif name == "network_qos" and stripped.startswith("rate:"):
            new_block.append(f"{indent}rate: 1mbit")
        elif name == "network_qos" and stripped.startswith("burst:"):
            new_block.append(f"{indent}burst: 32kb")
        elif name == "network_qos" and stripped.startswith("latency:"):
            new_block.append(f"{indent}latency: 50ms")
        else:
            new_block.append(item)
    out.extend(new_block)

if not seen:
    raise SystemExit("network_qos missing from skills.yaml")
path.write_text("\n".join(out) + "\n")
PY
}

create_lab_veth() {
    bash scripts/cleanup_network_qos_tc.sh "$QOS_IFACE" "$QOS_PEER" "$QOS_NETNS" >/dev/null 2>&1 || true
    ip link add "$QOS_IFACE" type veth peer name "$QOS_PEER"
    ip netns add "$QOS_NETNS"
    ip link set "$QOS_PEER" netns "$QOS_NETNS"
    ip addr add "$QOS_ADDR" dev "$QOS_IFACE"
    ip link set "$QOS_IFACE" up
    ip netns exec "$QOS_NETNS" ip addr add "$QOS_PEER_ADDR" dev "$QOS_PEER"
    ip netns exec "$QOS_NETNS" ip link set lo up
    ip netns exec "$QOS_NETNS" ip link set "$QOS_PEER" up
}

has_qdisc_residue() {
    tc qdisc show dev "$QOS_IFACE" 2>/dev/null | grep -Eq "tbf|clsact"
}

log "=== NetworkQos TC integration test ==="
cp "$SKILLS_YAML" "$RESULT_DIR/skills.yaml.bak"

if ! "$AGENT_BIN" --list-skills | grep -qx "network_qos"; then
    log "FAIL: network_qos skill is not registered"
    exit 1
fi
log "PASS: network_qos skill is registered"

create_lab_veth
ping -c 1 -W 1 -I "$QOS_IFACE" "$QOS_PEER_IP" > "$RESULT_DIR/baseline-ping.log" 2>&1
log "PASS: lab veth baseline connectivity works"

configure_network_qos audit
cp "$SKILLS_YAML" "$RESULT_DIR/skills.audit.yaml"
timeout 20s "$AGENT_BIN" --doctor-skills --config "$AGENT_YAML" > "$RESULT_DIR/audit-doctor.log" 2>&1
timeout 20s "$AGENT_BIN" --config "$AGENT_YAML" --duration-s 1 --interval-ms 1000 > "$RESULT_DIR/audit-agent.log" 2>&1
if has_qdisc_residue; then
    log "FAIL: audit mode changed TC qdisc unexpectedly"
    exit 1
fi
log "PASS: audit mode leaves TC qdisc unchanged"

configure_network_qos enforce
cp "$SKILLS_YAML" "$RESULT_DIR/skills.enforce.yaml"
timeout 20s "$AGENT_BIN" --doctor-skills --config "$AGENT_YAML" > "$RESULT_DIR/enforce-doctor.log" 2>&1
log "PASS: doctor succeeds with network_qos enforce mode"

timeout 25s "$AGENT_BIN" --config "$AGENT_YAML" --duration-s 8 --interval-ms 1000 \
    > "$RESULT_DIR/enforce-agent.log" 2>&1 &
AGENT_PID="$!"

ready="false"
for _ in $(seq 1 30); do
    if ! kill -0 "$AGENT_PID" 2>/dev/null; then
        wait "$AGENT_PID" 2>/dev/null || true
        AGENT_PID=""
        log "FAIL: enforce agent exited before TC setup"
        exit 1
    fi
    if tc qdisc show dev "$QOS_IFACE" 2>/dev/null | grep -q "tbf" &&
       tc qdisc show dev "$QOS_IFACE" 2>/dev/null | grep -q "clsact"; then
        ready="true"
        break
    fi
    sleep 0.2
done
if [ "$ready" != "true" ]; then
    tc qdisc show dev "$QOS_IFACE" > "$RESULT_DIR/qdisc-debug.log" 2>&1 || true
    tc filter show dev "$QOS_IFACE" egress > "$RESULT_DIR/filter-debug.log" 2>&1 || true
    log "FAIL: enforce mode did not install TC clsact + TBF"
    exit 1
fi
tc qdisc show dev "$QOS_IFACE" > "$RESULT_DIR/enforce-qdisc.log" 2>&1
tc filter show dev "$QOS_IFACE" egress > "$RESULT_DIR/enforce-filter.log" 2>&1 || true
log "PASS: enforce mode installs TC clsact + TBF"

ping -c 3 -W 1 -I "$QOS_IFACE" "$QOS_PEER_IP" > "$RESULT_DIR/enforce-ping.log" 2>&1
log "PASS: lab veth traffic passes through QoS path"

wait "$AGENT_PID"
AGENT_PID=""
log "PASS: enforce agent exits cleanly"

if has_qdisc_residue; then
    tc qdisc show dev "$QOS_IFACE" > "$RESULT_DIR/rollback-qdisc-debug.log" 2>&1 || true
    log "FAIL: rollback left TC qdisc residue"
    exit 1
fi
log "PASS: rollback leaves no TC qdisc residue"

python3 - <<'PY'
import json
from pathlib import Path

events = []
path = Path("reports/events/network_policy.jsonl")
if path.exists():
    for line in path.read_text().splitlines():
        try:
            item = json.loads(line)
        except json.JSONDecodeError:
            continue
        if item.get("skill") == "network_qos" and item.get("mode") == "enforce" and item.get("operation") == "rollback":
            events.append(item)
if not events:
    raise SystemExit("missing network_qos enforce rollback event")
packet_count = int(events[-1].get("evidence", {}).get("packet_count", "0"))
if packet_count <= 0:
    raise SystemExit("network_qos packet_count did not increase")
PY
log "PASS: network_qos rollback event records packet hits"

log "=== NetworkQos TC integration test complete ==="
