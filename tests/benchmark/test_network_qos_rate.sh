#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

AGENT_BIN="build/eulerpilot-agent"
AGENT_YAML="configs/agent.yaml"
SKILLS_YAML="configs/skills.yaml"
RATE_TOOL="$ROOT/tools/tcp_rate_probe.py"
QOS_IFACE="${QOS_IFACE:-ep-veth-qos0}"
QOS_PEER="${QOS_PEER:-ep-veth-qos1}"
QOS_NETNS="${QOS_NETNS:-ep-qos-ns}"
QOS_ADDR="${QOS_ADDR:-10.88.0.1/24}"
QOS_PEER_ADDR="${QOS_PEER_ADDR:-10.88.0.2/24}"
QOS_PEER_IP="${QOS_PEER_IP:-10.88.0.2}"
QOS_RATE="${QOS_RATE:-2mbit}"
QOS_RATE_MBIT="${QOS_RATE_MBIT:-2.0}"
PROBE_PORT="${PROBE_PORT:-19091}"
PROBE_DURATION="${PROBE_DURATION:-6}"
PROBE_CHUNK_SIZE="${PROBE_CHUNK_SIZE:-1200}"
RESULT_DIR="results/network_policy/qos-rate-$(date +%Y%m%d-%H%M%S)"
AGENT_PID=""
SERVER_PID=""
mkdir -p "$RESULT_DIR"

log() {
    echo "$*" | tee -a "$RESULT_DIR/benchmark.log"
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
    bash scripts/cleanup_network_qos_tc.sh "$QOS_IFACE" "$QOS_PEER" "$QOS_NETNS" >/dev/null 2>&1 || true
}
trap restore EXIT

configure_network_qos() {
    local mode="$1"

    NETWORK_QOS_MODE="$mode" \
    NETWORK_QOS_IFACE="$QOS_IFACE" \
    NETWORK_QOS_RATE="$QOS_RATE" \
    python3 - <<'PY'
import os
from pathlib import Path

path = Path("configs/skills.yaml")
lines = path.read_text().splitlines()
mode = os.environ["NETWORK_QOS_MODE"]
ifname = os.environ["NETWORK_QOS_IFACE"]
rate = os.environ["NETWORK_QOS_RATE"]

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
            elif name in ("network_policy", "network_policy_demo",
                          "network_xdp", "security_policy_demo"):
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
            new_block.append(f"{indent}rate: {rate}")
        elif name == "network_qos" and stripped.startswith("burst:"):
            new_block.append(f"{indent}burst: 64kb")
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
    disable_veth_offload
}

disable_veth_offload() {
    if ! command -v ethtool >/dev/null 2>&1; then
        return 0
    fi
    ethtool -K "$QOS_IFACE" tso off gso off gro off 2>/dev/null || true
    ip netns exec "$QOS_NETNS" ethtool -K "$QOS_PEER" tso off gso off gro off 2>/dev/null || true
}

has_qdisc_residue() {
    tc qdisc show dev "$QOS_IFACE" 2>/dev/null | grep -Eq "tbf|clsact"
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

run_probe() {
    local label="$1"
    local ready="$RESULT_DIR/$label.ready"
    rm -f "$ready"

    ip netns exec "$QOS_NETNS" python3 "$RATE_TOOL" server \
        --bind "$QOS_PEER_IP" \
        --port "$PROBE_PORT" \
        --max-duration-s "$((PROBE_DURATION + 8))" \
        --chunk-size "$PROBE_CHUNK_SIZE" \
        --ready-file "$ready" \
        --json-output "$ROOT/$RESULT_DIR/$label-server.json" \
        > "$RESULT_DIR/$label-server.log" 2>&1 &
    SERVER_PID="$!"

    if ! wait_for_file "$ready"; then
        log "FAIL: $label server did not become ready"
        exit 1
    fi

    python3 "$RATE_TOOL" client \
        --host "$QOS_PEER_IP" \
        --port "$PROBE_PORT" \
        --duration-s "$PROBE_DURATION" \
        --chunk-size "$PROBE_CHUNK_SIZE" \
        --json-output "$ROOT/$RESULT_DIR/$label-client.json" \
        > "$RESULT_DIR/$label-client.log" 2>&1

    wait "$SERVER_PID"
    SERVER_PID=""
}

log "=== NetworkQos rate benchmark ==="
cp "$SKILLS_YAML" "$RESULT_DIR/skills.yaml.bak"

if [ ! -x "$AGENT_BIN" ]; then
    log "FAIL: $AGENT_BIN missing; run make agent first"
    exit 1
fi
if [ ! -f "$RATE_TOOL" ]; then
    log "FAIL: $RATE_TOOL missing"
    exit 1
fi
if ! "$AGENT_BIN" --list-skills | grep -qx "network_qos"; then
    log "FAIL: network_qos skill is not registered"
    exit 1
fi

create_lab_veth
ping -c 1 -W 1 -I "$QOS_IFACE" "$QOS_PEER_IP" > "$RESULT_DIR/baseline-ping.log" 2>&1
log "PASS: lab veth baseline connectivity works"

run_probe baseline
log "PASS: baseline TCP throughput probe complete"

configure_network_qos enforce
cp "$SKILLS_YAML" "$RESULT_DIR/skills.enforce.yaml"
timeout 20s "$AGENT_BIN" --doctor-skills --config "$AGENT_YAML" > "$RESULT_DIR/enforce-doctor.log" 2>&1
log "PASS: doctor succeeds with network_qos enforce mode"

timeout 30s "$AGENT_BIN" --config "$AGENT_YAML" --duration-s "$((PROBE_DURATION + 8))" --interval-ms 1000 \
    > "$RESULT_DIR/enforce-agent.log" 2>&1 &
AGENT_PID="$!"

ready="false"
for _ in $(seq 1 40); do
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
    log "FAIL: enforce mode did not install TC clsact + TBF"
    exit 1
fi
tc qdisc show dev "$QOS_IFACE" > "$RESULT_DIR/enforce-qdisc.log" 2>&1

run_probe enforce
log "PASS: enforce TCP throughput probe complete"

wait "$AGENT_PID"
AGENT_PID=""
log "PASS: enforce agent exits cleanly"

if has_qdisc_residue; then
    tc qdisc show dev "$QOS_IFACE" > "$RESULT_DIR/rollback-qdisc-debug.log" 2>&1 || true
    log "FAIL: rollback left TC qdisc residue"
    exit 1
fi
log "PASS: rollback leaves no TC qdisc residue"

RESULT_DIR="$RESULT_DIR" QOS_RATE_MBIT="$QOS_RATE_MBIT" python3 - <<'PY'
import csv
import json
import os
from pathlib import Path

result_dir = Path(os.environ.get("RESULT_DIR", ""))
if not result_dir:
    result_dir = sorted(Path("results/network_policy").glob("qos-rate-*"))[-1]

target = float(os.environ["QOS_RATE_MBIT"])
baseline = json.loads((result_dir / "baseline-server.json").read_text())
enforce = json.loads((result_dir / "enforce-server.json").read_text())
baseline_mbps = float(baseline["mbps"])
enforce_mbps = float(enforce["mbps"])
error_pct = (enforce_mbps - target) / target * 100.0
reduction_ratio = baseline_mbps / enforce_mbps if enforce_mbps > 0 else 0.0
status = "PASS"
reasons = []
if baseline_mbps < target * 2.0:
    status = "FAIL"
    reasons.append("baseline throughput is too close to target")
if enforce_mbps > target * 1.80:
    status = "FAIL"
    reasons.append("enforced throughput is above allowed bound")
if enforce_mbps >= baseline_mbps * 0.75:
    status = "FAIL"
    reasons.append("enforced throughput did not drop enough")

summary = {
    "target_mbps": target,
    "baseline_mbps": baseline_mbps,
    "enforce_mbps": enforce_mbps,
    "error_pct": error_pct,
    "reduction_ratio": reduction_ratio,
    "status": status,
    "reasons": "; ".join(reasons),
}
(result_dir / "summary.json").write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")
with (result_dir / "summary.csv").open("w", newline="") as f:
    writer = csv.DictWriter(f, fieldnames=list(summary))
    writer.writeheader()
    writer.writerow(summary)
(result_dir / "README.md").write_text(
    "# NetworkQos rate benchmark\n\n"
    f"Target rate: `{target:.2f} Mbit/s`\n\n"
    f"- Baseline throughput: `{baseline_mbps:.3f} Mbit/s`\n"
    f"- Enforced throughput: `{enforce_mbps:.3f} Mbit/s`\n"
    f"- Error vs target: `{error_pct:.2f}%`\n"
    f"- Baseline/enforce reduction ratio: `{reduction_ratio:.2f}x`\n"
    f"- Status: `{status}`\n\n"
    "Reproduce:\n\n"
    "```bash\n"
    "make agent network-qos-tc\n"
    "bash tests/benchmark/test_network_qos_rate.sh\n"
    "```\n"
)
print(json.dumps(summary, sort_keys=True))
if status != "PASS":
    raise SystemExit(1)
PY
log "PASS: rate benchmark summary generated"

log "=== NetworkQos rate benchmark complete ==="
