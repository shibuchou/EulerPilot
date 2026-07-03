#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

AGENT_BIN="build/eulerpilot-agent"
AGENT_YAML="configs/agent.yaml"
SKILLS_YAML="configs/skills.yaml"
RATE_TOOL="$ROOT/tools/tcp_rate_probe.py"
XDP_IFACE="ep-veth-xdp0"
XDP_PEER="ep-veth-xdp1"
XDP_NETNS="ep-xdp-ns"
XDP_ADDR="10.89.0.1/24"
XDP_PEER_ADDR="10.89.0.2/24"
XDP_HOST_IP="10.89.0.1"
XDP_TCP_PORT="19092"
XDP_UDP_PORT="19093"
XDP_UDP_TUPLE_PORT="19094"
XDP_UDP_TUPLE_SRC_PORT="39094"
RESULT_DIR="${RESULT_DIR:-results/network_policy/xdp-$(date +%Y%m%d-%H%M%S)}"
AGENT_PID=""
SERVER_PID=""
EVENT_LINES_BEFORE=0
mkdir -p "$RESULT_DIR"

log() {
    echo "$*" | tee -a "$RESULT_DIR/test.log"
}

restore() {
    if [ -n "${SERVER_PID:-}" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    if [ -n "${AGENT_PID:-}" ] && kill -0 "$AGENT_PID" 2>/dev/null; then
        kill "$AGENT_PID" 2>/dev/null || true
        wait "$AGENT_PID" 2>/dev/null || true
    fi
    if [ -f "$RESULT_DIR/skills.yaml.bak" ]; then
        cp "$RESULT_DIR/skills.yaml.bak" "$SKILLS_YAML"
    fi
    bash scripts/cleanup_network_xdp_demo.sh "$XDP_IFACE" "$XDP_PEER" "$XDP_NETNS" >/dev/null 2>&1 || true
}
trap restore EXIT

configure_network_xdp() {
    local mode="$1"

    NETWORK_XDP_MODE="$mode" \
    NETWORK_XDP_IFACE="$XDP_IFACE" \
    python3 - <<'PY'
import os
from pathlib import Path

path = Path("configs/skills.yaml")
lines = path.read_text().splitlines()
mode = os.environ["NETWORK_XDP_MODE"]
ifname = os.environ["NETWORK_XDP_IFACE"]

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

    if name == "network_xdp":
        seen = True

    new_block = []
    for item in block:
        stripped = item.strip()
        indent = item[:len(item) - len(item.lstrip())]
        if stripped.startswith("enabled:"):
            if name == "network_xdp":
                enabled = "true"
            elif name in ("network_policy", "network_policy_demo",
                          "network_qos", "security_policy_demo"):
                enabled = "false"
            else:
                enabled = stripped.split(":", 1)[1].strip()
            new_block.append(f"{indent}enabled: {enabled}")
        elif name == "network_xdp" and stripped.startswith("mode:"):
            new_block.append(f"{indent}mode: {mode}")
        elif name == "network_xdp" and stripped.startswith("ifname:"):
            new_block.append(f"{indent}ifname: {ifname}")
        else:
            new_block.append(item)
    out.extend(new_block)

if not seen:
    raise SystemExit("network_xdp missing from skills.yaml")
path.write_text("\n".join(out) + "\n")
PY
}

create_lab_veth() {
    bash scripts/cleanup_network_xdp_demo.sh "$XDP_IFACE" "$XDP_PEER" "$XDP_NETNS" >/dev/null 2>&1 || true
    ip link add "$XDP_IFACE" type veth peer name "$XDP_PEER"
    ip netns add "$XDP_NETNS"
    ip link set "$XDP_PEER" netns "$XDP_NETNS"
    ip addr add "$XDP_ADDR" dev "$XDP_IFACE"
    ip link set "$XDP_IFACE" up
    ip netns exec "$XDP_NETNS" ip addr add "$XDP_PEER_ADDR" dev "$XDP_PEER"
    ip netns exec "$XDP_NETNS" ip link set lo up
    ip netns exec "$XDP_NETNS" ip link set "$XDP_PEER" up
}

has_xdp_attached() {
    ip -d link show "$XDP_IFACE" 2>/dev/null |
        grep -Eqi "prog/xdp|xdpgeneric|xdpdrv|xdpoffload|xdp/id"
}

ping_host_from_netns() {
    ip netns exec "$XDP_NETNS" ping -c 1 -W 1 "$XDP_HOST_IP"
}

wait_for_file() {
    local path="$1"
    for _ in $(seq 1 50); do
        if [ -s "$path" ]; then
            return 0
        fi
        sleep 0.1
    done
    return 1
}

start_tcp_server() {
    local label="$1"
    local ready="$RESULT_DIR/$label.ready"
    rm -f "$ready"
    python3 "$RATE_TOOL" server \
        --bind "$XDP_HOST_IP" \
        --port "$XDP_TCP_PORT" \
        --max-duration-s 8 \
        --ready-file "$ready" \
        --json-output "$ROOT/$RESULT_DIR/$label-server.json" \
        > "$RESULT_DIR/$label-server.log" 2>&1 &
    SERVER_PID="$!"
    if ! wait_for_file "$ready"; then
        log "FAIL: $label TCP server did not become ready"
        exit 1
    fi
}

run_tcp_client_from_netns() {
    local label="$1"
    timeout 4s ip netns exec "$XDP_NETNS" python3 "$RATE_TOOL" client \
        --host "$XDP_HOST_IP" \
        --port "$XDP_TCP_PORT" \
        --duration-s 0.2 \
        --json-output "$ROOT/$RESULT_DIR/$label-client.json" \
        > "$RESULT_DIR/$label-client.log" 2>&1
}

tcp_probe_should_drop() {
    local label="$1"
    if run_tcp_client_from_netns "$label"; then
        log "FAIL: TCP probe unexpectedly passed while XDP drop is active"
        exit 1
    fi
}

send_udp_from_netns() {
    local label="$1"
    local dst_port="${2:-$XDP_UDP_PORT}"
    local src_port="${3:-0}"
    timeout 4s ip netns exec "$XDP_NETNS" python3 - "$XDP_HOST_IP" "$dst_port" "$src_port" \
        > "$RESULT_DIR/$label-udp-client.log" 2>&1 <<'PY'
import socket
import sys
import time

host = sys.argv[1]
port = int(sys.argv[2])
src_port = int(sys.argv[3])
payload = b"eulerpilot-xdp-udp-probe" * 4

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
if src_port != 0:
    sock.bind(("10.89.0.2", src_port))
for _ in range(8):
    sock.sendto(payload, (host, port))
    time.sleep(0.02)
sock.close()
PY
}

event_line_count() {
    if [ -f reports/events/network_policy.jsonl ]; then
        wc -l < reports/events/network_policy.jsonl
    else
        echo 0
    fi
}

log "=== NetworkXDP integration test ==="
cp "$SKILLS_YAML" "$RESULT_DIR/skills.yaml.bak"

if ! "$AGENT_BIN" --list-skills | grep -qx "network_xdp"; then
    log "FAIL: network_xdp skill is not registered"
    exit 1
fi
if [ ! -f "$RATE_TOOL" ]; then
    log "FAIL: $RATE_TOOL missing"
    exit 1
fi
log "PASS: network_xdp skill is registered"

create_lab_veth
ping_host_from_netns > "$RESULT_DIR/baseline-ping.log" 2>&1
log "PASS: lab veth baseline connectivity works"

configure_network_xdp audit
cp "$SKILLS_YAML" "$RESULT_DIR/skills.audit.yaml"
timeout 20s "$AGENT_BIN" --doctor-skills --config "$AGENT_YAML" > "$RESULT_DIR/audit-doctor.log" 2>&1
timeout 20s "$AGENT_BIN" --config "$AGENT_YAML" --duration-s 1 --interval-ms 1000 > "$RESULT_DIR/audit-agent.log" 2>&1
if has_xdp_attached; then
    ip -d link show "$XDP_IFACE" > "$RESULT_DIR/audit-link-debug.log" 2>&1 || true
    log "FAIL: audit mode attached XDP unexpectedly"
    exit 1
fi
log "PASS: audit mode leaves XDP detached"

configure_network_xdp enforce
cp "$SKILLS_YAML" "$RESULT_DIR/skills.enforce.yaml"
EVENT_LINES_BEFORE="$(event_line_count)"
timeout 20s "$AGENT_BIN" --doctor-skills --config "$AGENT_YAML" > "$RESULT_DIR/enforce-doctor.log" 2>&1
log "PASS: doctor succeeds with network_xdp enforce mode"

timeout 25s "$AGENT_BIN" --config "$AGENT_YAML" --duration-s 8 --interval-ms 1000 \
    > "$RESULT_DIR/enforce-agent.log" 2>&1 &
AGENT_PID="$!"

ready="false"
for _ in $(seq 1 30); do
    if ! kill -0 "$AGENT_PID" 2>/dev/null; then
        wait "$AGENT_PID" 2>/dev/null || true
        AGENT_PID=""
        log "FAIL: enforce agent exited before XDP setup"
        exit 1
    fi
    if has_xdp_attached; then
        ready="true"
        break
    fi
    sleep 0.2
done
if [ "$ready" != "true" ]; then
    ip -d link show "$XDP_IFACE" > "$RESULT_DIR/xdp-debug.log" 2>&1 || true
    log "FAIL: enforce mode did not attach XDP"
    exit 1
fi
ip -d link show "$XDP_IFACE" > "$RESULT_DIR/enforce-link.log" 2>&1
log "PASS: enforce mode attaches XDP generic program"

if ping_host_from_netns > "$RESULT_DIR/enforce-ping-drop.log" 2>&1; then
    log "FAIL: ICMP ping unexpectedly passed while XDP drop is active"
    exit 1
fi
log "PASS: XDP drop blocks ICMP in isolated veth lab"

tcp_probe_should_drop enforce-tcp-drop
log "PASS: XDP drop blocks TCP:$XDP_TCP_PORT in isolated veth lab"

send_udp_from_netns enforce-udp-drop "$XDP_UDP_PORT" 0
log "PASS: XDP drop records UDP:$XDP_UDP_PORT in isolated veth lab"

send_udp_from_netns enforce-udp-tuple-drop "$XDP_UDP_TUPLE_PORT" "$XDP_UDP_TUPLE_SRC_PORT"
log "PASS: XDP drop records UDP tuple $XDP_PEER_ADDR -> $XDP_HOST_IP:$XDP_UDP_TUPLE_PORT in isolated veth lab"

wait "$AGENT_PID"
AGENT_PID=""
log "PASS: enforce agent exits cleanly"

if has_xdp_attached; then
    ip -d link show "$XDP_IFACE" > "$RESULT_DIR/rollback-link-debug.log" 2>&1 || true
    log "FAIL: rollback left XDP attached"
    exit 1
fi
log "PASS: rollback leaves no XDP attachment"

ping_host_from_netns > "$RESULT_DIR/rollback-ping.log" 2>&1
log "PASS: connectivity recovers after rollback"

if [ -f reports/events/network_policy.jsonl ]; then
    tail -n +"$((EVENT_LINES_BEFORE + 1))" reports/events/network_policy.jsonl \
        > "$RESULT_DIR/network_policy_events.jsonl"
else
    : > "$RESULT_DIR/network_policy_events.jsonl"
fi
if [ -f run/eulerpilot/action_journal.jsonl ]; then
    grep '"skill":"network_xdp"' run/eulerpilot/action_journal.jsonl \
        > "$RESULT_DIR/action_journal.network_xdp.jsonl" || true
fi

NETWORK_XDP_EVENT_OFFSET="$EVENT_LINES_BEFORE" \
NETWORK_XDP_RESULT_DIR="$RESULT_DIR" \
python3 - <<'PY'
import json
import os
from pathlib import Path

offset = int(os.environ["NETWORK_XDP_EVENT_OFFSET"])
result_dir = Path(os.environ["NETWORK_XDP_RESULT_DIR"])
events = []
path = Path("reports/events/network_policy.jsonl")
if path.exists():
    for line in path.read_text().splitlines()[offset:]:
        try:
            item = json.loads(line)
        except json.JSONDecodeError:
            continue
        if item.get("skill") == "network_xdp" and item.get("mode") == "enforce" and item.get("operation") == "rollback":
            events.append(item)
if not events:
    raise SystemExit("missing network_xdp enforce rollback event")
evidence = events[-1].get("evidence", {})
drop_count = int(evidence.get("drop_count", "0"))
if drop_count < 4:
    raise SystemExit("network_xdp drop_count did not increase for ICMP/TCP/UDP/tuple")
for rule in ("drop_icmp_lab", "drop_tcp_probe_lab", "drop_udp_probe_lab", "drop_udp_tuple_lab"):
    key = f"rule.{rule}.drop_count"
    if int(evidence.get(key, "0")) < 1:
        raise SystemExit(f"network_xdp per-rule drop counter missing for {rule}")
if evidence.get("rule.drop_udp_tuple_lab.src_ip") != "10.89.0.2":
    raise SystemExit("network_xdp tuple rule missing src_ip evidence")
if evidence.get("rule.drop_udp_tuple_lab.dst_ip") != "10.89.0.1":
    raise SystemExit("network_xdp tuple rule missing dst_ip evidence")
if evidence.get("rule.drop_udp_tuple_lab.src_port") != "39094":
    raise SystemExit("network_xdp tuple rule missing src_port evidence")
if evidence.get("rule.drop_udp_tuple_lab.dst_port") != "19094":
    raise SystemExit("network_xdp tuple rule missing dst_port evidence")
result_dir.joinpath("xdp_rule_stats.txt").write_text(
    "\n".join([
        f"total_drop_count={drop_count}",
        f"drop_icmp_lab={evidence.get('rule.drop_icmp_lab.drop_count', '0')}",
        f"drop_tcp_probe_lab={evidence.get('rule.drop_tcp_probe_lab.drop_count', '0')}",
        f"drop_udp_probe_lab={evidence.get('rule.drop_udp_probe_lab.drop_count', '0')}",
        f"drop_udp_tuple_lab={evidence.get('rule.drop_udp_tuple_lab.drop_count', '0')}",
        f"drop_udp_tuple_lab.src_ip={evidence.get('rule.drop_udp_tuple_lab.src_ip', '')}",
        f"drop_udp_tuple_lab.dst_ip={evidence.get('rule.drop_udp_tuple_lab.dst_ip', '')}",
        f"drop_udp_tuple_lab.src_port={evidence.get('rule.drop_udp_tuple_lab.src_port', '')}",
        f"drop_udp_tuple_lab.dst_port={evidence.get('rule.drop_udp_tuple_lab.dst_port', '')}",
        f"rule_stats={evidence.get('rule_stats', '')}",
    ]) + "\n"
)
PY
log "PASS: network_xdp rollback event records ICMP/TCP/UDP and tuple per-rule drops"

cat > "$RESULT_DIR/summary.txt" <<EOF
result=pass
host=$(hostname)
ifname=$XDP_IFACE
rules=drop_icmp_lab,drop_tcp_probe_lab,drop_udp_probe_lab,drop_udp_tuple_lab
tcp_port=$XDP_TCP_PORT
udp_port=$XDP_UDP_PORT
udp_tuple_src_ip=10.89.0.2
udp_tuple_dst_ip=$XDP_HOST_IP
udp_tuple_src_port=$XDP_UDP_TUPLE_SRC_PORT
udp_tuple_dst_port=$XDP_UDP_TUPLE_PORT
events=network_policy_events.jsonl
journal=action_journal.network_xdp.jsonl
EOF

cat > "$RESULT_DIR/README.md" <<EOF
# NetworkXDP integration result

Run directory: \`$RESULT_DIR\`

This test validates the \`network_xdp\` sub-skill on an isolated veth pair.

- Baseline: netns peer can ping host-side veth.
- Audit: Agent starts without attaching XDP.
- Enforce: Agent attaches XDP generic mode on \`$XDP_IFACE\`, blocks ICMP connectivity, and records TCP:\`$XDP_TCP_PORT\`, UDP:\`$XDP_UDP_PORT\`, plus UDP tuple \`10.89.0.2:$XDP_UDP_TUPLE_SRC_PORT -> $XDP_HOST_IP:$XDP_UDP_TUPLE_PORT\` rule hits.
- Rollback: Agent detaches XDP and connectivity recovers.

Reproduce:

\`\`\`bash
make agent network-xdp-demo
bash tests/integration/test_network_xdp.sh
\`\`\`
EOF

log "=== NetworkXDP integration test complete ==="
