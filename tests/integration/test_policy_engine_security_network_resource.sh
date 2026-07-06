#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR"

ROOT="/sys/fs/cgroup/eulerpilot"
TARGET="$ROOT/policy-engine-v3-resource"
QOS_IFACE="ep-veth-pe0"
QOS_PEER="ep-veth-pe1"
QOS_NETNS="ep-pe-ns"
QOS_ADDR="10.89.0.1/24"
QOS_PEER_ADDR="10.89.0.2/24"
QOS_PEER_IP="10.89.0.2"
RESULT_BASE="${RESULT_DIR:-results/policy_engine/security-network-resource-$(date +%Y%m%d-%H%M%S)}"
REPEAT=1
AGENT_PID=""
TARGET_PID=""
SERVER_PID=""

while [ $# -gt 0 ]; do
    case "$1" in
        --repeat)
            shift
            REPEAT="${1:-}"
            [ -n "$REPEAT" ] || { echo '[FAIL] --repeat requires a value' >&2; exit 1; }
            ;;
        --repeat=*)
            REPEAT="${1#*=}"
            ;;
        *)
            echo "[FAIL] unknown argument: $1" >&2
            exit 1
            ;;
    esac
    shift
done

fail() {
    printf '[FAIL] %s\n' "$*" >&2
    exit 1
}

info() {
    printf '[INFO] %s\n' "$*"
}

cleanup_processes() {
    set +e
    if [ -n "${AGENT_PID:-}" ] && kill -0 "$AGENT_PID" 2>/dev/null; then
        kill "$AGENT_PID" 2>/dev/null || true
        wait "$AGENT_PID" 2>/dev/null || true
    fi
    if [ -n "${TARGET_PID:-}" ] && kill -0 "$TARGET_PID" 2>/dev/null; then
        kill "$TARGET_PID" 2>/dev/null || true
        wait "$TARGET_PID" 2>/dev/null || true
    fi
    if [ -n "${SERVER_PID:-}" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    AGENT_PID=""
    TARGET_PID=""
    SERVER_PID=""
}

cleanup_lab() {
    set +e
    cleanup_processes
    tc qdisc del dev "$QOS_IFACE" root >/dev/null 2>&1 || true
    bash scripts/cleanup_network_qos_tc.sh "$QOS_IFACE" "$QOS_PEER" "$QOS_NETNS" >/dev/null 2>&1 || true
    scripts/rollback.sh >/dev/null 2>&1 || true
    rmdir "$TARGET" >/dev/null 2>&1 || true
}
trap cleanup_lab EXIT

wait_for_file_value() {
    local file="$1"
    local expected="$2"
    local deadline=$((SECONDS + 25))
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

wait_for_grep() {
    local pattern="$1"
    local file="$2"
    local deadline=$((SECONDS + 25))
    while [ "$SECONDS" -lt "$deadline" ]; do
        if [ -f "$file" ] && grep -q "$pattern" "$file"; then
            return 0
        fi
        sleep 0.2
    done
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

write_probe_tools() {
    local dir="$1"
    cat > "$dir/tcp_sink.py" <<'PY'
import socket, sys, time
host = sys.argv[1]
port = int(sys.argv[2])
duration = float(sys.argv[3])
end = time.time() + duration + 2.0
received = 0
srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind((host, port))
srv.listen(1)
srv.settimeout(duration + 5.0)
conn, _ = srv.accept()
conn.settimeout(1.0)
while time.time() < end:
    try:
        data = conn.recv(65536)
    except socket.timeout:
        continue
    if not data:
        break
    received += len(data)
conn.close()
srv.close()
print(received)
PY
    cat > "$dir/tcp_push.py" <<'PY'
import socket, sys, time
host = sys.argv[1]
port = int(sys.argv[2])
duration = float(sys.argv[3])
payload = b'x' * 65536
sent = 0
sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.settimeout(5.0)
sock.connect((host, port))
end = time.time() + duration
while time.time() < end:
    try:
        sent += sock.send(payload)
    except (BlockingIOError, InterruptedError):
        continue
sock.shutdown(socket.SHUT_WR)
sock.close()
mbit = sent * 8.0 / duration / 1000000.0
print(f"mbit_per_sec={mbit:.3f}")
PY
}

measure_python_rate() {
    local dir="$1"
    local label="$2"
    local port="$3"
    local duration=3
    ip netns exec "$QOS_NETNS" python3 "$dir/tcp_sink.py" "$QOS_PEER_IP" "$port" "$duration" \
        > "$dir/${label}_server_bytes.txt" 2> "$dir/${label}_server.err" &
    SERVER_PID="$!"
    sleep 0.4
    python3 "$dir/tcp_push.py" "$QOS_PEER_IP" "$port" "$duration" \
        > "$dir/${label}_rate.txt" 2> "$dir/${label}_client.err" || true
    wait "$SERVER_PID" 2>/dev/null || true
    SERVER_PID=""
}

measure_rate() {
    local dir="$1"
    local label="$2"
    local port="$3"
    if command -v iperf3 >/dev/null 2>&1; then
        ip netns exec "$QOS_NETNS" iperf3 -s -1 -p "$port" \
            > "$dir/${label}_iperf_server.txt" 2>&1 &
        SERVER_PID="$!"
        sleep 0.5
        iperf3 -c "$QOS_PEER_IP" -p "$port" -t 3 -f m \
            > "$dir/${label}_rate.txt" 2>&1 || true
        wait "$SERVER_PID" 2>/dev/null || true
        SERVER_PID=""
    else
        measure_python_rate "$dir" "$label" "$port"
    fi
}

parse_rate_mbit() {
    local file="$1"
    python3 - "$file" <<'PY'
import re, sys
text = open(sys.argv[1], encoding='utf-8', errors='ignore').read()
values = [float(x) for x in re.findall(r'mbit_per_sec=([0-9.]+)', text, re.I)]
if not values:
    values = [float(x) for x in re.findall(r'([0-9.]+)\s*Mbits/sec', text, re.I)]
if values:
    print(values[-1])
PY
}

verify_rate_limit() {
    local dir="$1"
    local before after
    before="$(parse_rate_mbit "$dir/before_rate.txt" || true)"
    after="$(parse_rate_mbit "$dir/after_rate.txt" || true)"
    printf 'before_mbit=%s\nafter_mbit=%s\n' "$before" "$after" > "$dir/rate_summary.txt"
    [ -n "$before" ] || fail 'cannot parse baseline network rate'
    [ -n "$after" ] || fail 'cannot parse limited network rate'
    python3 - "$before" "$after" <<'PY'
import sys
before = float(sys.argv[1])
after = float(sys.argv[2])
if after >= before:
    raise SystemExit('limited throughput is not below baseline')
if after > 3.0:
    raise SystemExit(f'limited throughput too high for fallback probe: {after} mbit/s')
PY
}

trigger_burst_connect() {
    python3 - <<'PY'
import socket
srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(("127.0.0.1", 65000))
srv.listen(8)
for _ in range(6):
    cli = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    cli.settimeout(1.0)
    cli.connect(("127.0.0.1", 65000))
    conn, _ = srv.accept()
    conn.close()
    cli.close()
srv.close()
PY
}
write_agent_config() {
    local dir="$1"
    cat > "$dir/agent.yaml" <<'YAML'
agent:
  name: EulerPilot
  mode: active
skills_config_path: skills.yaml
scheduler:
  type: cgroup_v2
exporter:
  prometheus:
    enabled: false
YAML
    cat > "$dir/skills.yaml" <<YAML
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
- name: network_qos
  kind: runtime
  enabled: true
  config:
    mode: audit
    targets:
      lab_netdev:
        type: netdev
        ifname: $QOS_IFACE
    rules:
      - name: policy_engine_lab_qos_guard
        hook: tc_egress
        target_ref: lab_netdev
        protocol: any
        dst_port: '0'
        rate: 2mbit
        burst: 32kb
        latency: 50ms
        action: limit
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
      lab_connect:
        type: path
        dst_ip: 127.0.0.1
        dst_port: '65000'
    rules:
      - name: observe_demo_secret_open
        hook: lsm_file_open
        target_ref: demo_secret
        action: deny
      - name: observe_lab_connect
        hook: lsm_socket_connect
        target_ref: lab_connect
        action: deny
    anomaly_rules:
      - name: burst_connect
        type: rate
        syscall: connect
        threshold: 3
        window_ms: 1000
        severity: high
      - name: burst_openat_sensitive
        type: rate
        syscall: openat
        path_prefix: /etc
        threshold: 3
        window_ms: 1000
        severity: medium
      - name: capability_abuse
        type: rate
        syscall: capability
        threshold: 3
        window_ms: 1000
        severity: high
- name: policy_engine
  kind: runtime
  enabled: true
  config:
    mode: enforce
    policy_id: security_network_resource_response
    source:
      audit_path: reports/events/security_policy.jsonl
    watch:
      skill: security_policy
      operation: anomaly
      rule_id: burst_connect
      result: observed
    dependencies:
      security_policy: true
      resource_control: true
      network_qos: true
    guards:
      memory_high: true
    targets:
      demo_cgroup:
        type: cgroup
        path: $TARGET
      lab_netdev:
        type: netdev
        ifname: $QOS_IFACE
    actions:
      - name: throttle_demo_cgroup_cpu
        target_ref: demo_cgroup
        file: cpu.max
        value: '20000 100000'
      - name: cap_demo_cgroup_memory
        target_ref: demo_cgroup
        file: memory.high
        value: '134217728'
      - name: limit_lab_netdev
        target_ref: lab_netdev
        file: network_qos.rate
        value: 2mbit
        burst: 32kb
        latency: 50ms
YAML
}

extract_transaction_id() {
    python3 - <<'PY'
import json
from pathlib import Path
for line in Path('reports/events/policy_engine.jsonl').read_text().splitlines():
    if not line.strip():
        continue
    item = json.loads(line)
    txn = item.get('transaction_id')
    if txn:
        print(txn)
        break
PY
}

verify_transaction_chain() {
    local txn="$1"
    [ -n "$txn" ] || fail 'missing transaction_id'
    grep -q "\"transaction_id\":\"$txn\"" reports/events/policy_engine.jsonl || fail 'transaction missing policy_engine events'
    grep -q "\"transaction_id\":\"$txn\"" reports/events/resource_control.jsonl || fail 'transaction missing resource_control events'
    grep -q "\"transaction_id\":\"$txn\"" reports/events/network_policy.jsonl || fail 'transaction missing network_qos events'
    grep -q "\"transaction_id\":\"$txn\"" run/eulerpilot/action_journal.jsonl || fail 'transaction missing ActionJournal records'
}

run_success_case() {
    local iter="$1"
    local dir="$RESULT_BASE/iter-$iter"
    mkdir -p "$dir" reports/events run/eulerpilot
    : > reports/events/security_policy.jsonl
    : > reports/events/policy_engine.jsonl
    : > reports/events/resource_control.jsonl
    : > reports/events/network_policy.jsonl
    : > run/eulerpilot/action_journal.jsonl

    cleanup_processes
    tc qdisc del dev "$QOS_IFACE" root >/dev/null 2>&1 || true
    bash scripts/cleanup_network_qos_tc.sh "$QOS_IFACE" "$QOS_PEER" "$QOS_NETNS" >/dev/null 2>&1 || true
    create_lab_veth
    init_leaf_cgroup "$TARGET"
    [ -w "$TARGET/cpu.max" ] || fail "$TARGET/cpu.max is not writable"
    [ -w "$TARGET/memory.high" ] || fail "$TARGET/memory.high is not writable"

    OLD_CPU_MAX="$(cat "$TARGET/cpu.max")"
    OLD_MEMORY_HIGH="$(cat "$TARGET/memory.high")"

    yes > /dev/null &
    TARGET_PID="$!"
    echo "$TARGET_PID" > "$TARGET/cgroup.procs"

    write_probe_tools "$dir"
    measure_rate "$dir" before 19091
    cp "$dir/before_rate.txt" "$dir/before_rate.raw" 2>/dev/null || true
    tc qdisc show dev "$QOS_IFACE" > "$dir/tc_qdisc_before.txt" 2>&1 || true
    write_agent_config "$dir"

    timeout 30s ./build/eulerpilot-agent \
        --config "$dir/agent.yaml" \
        --backend cgroup_v2 \
        --gate-mode normal \
        --active \
        --duration-s 12 \
        --interval-ms 500 \
        --jsonl \
        > "$dir/agent.log" 2>&1 &
    AGENT_PID="$!"
    sleep 1
    kill -0 "$AGENT_PID" 2>/dev/null || fail "agent exited early; see $dir/agent.log"
    wait_for_grep '"skill":"security_policy".*"operation":"start"' reports/events/security_policy.jsonl || \
        fail 'security_policy did not reach start state before trigger'
    wait_for_grep '"skill":"policy_engine".*"operation":"start"' reports/events/policy_engine.jsonl || \
        fail 'policy_engine did not reach start state before trigger'
    sleep 1

    trigger_burst_connect
    wait_for_file_value "$TARGET/cpu.max" '20000 100000' || fail 'policy_engine did not apply cpu.max response'
    wait_for_file_value "$TARGET/memory.high" '134217728' || fail 'policy_engine did not apply memory.high response'
    wait_for_grep '"skill":"network_qos".*"result":"applied"' reports/events/network_policy.jsonl || \
        fail 'policy_engine did not emit network_qos applied response'

    tc qdisc show dev "$QOS_IFACE" > "$dir/tc_qdisc_after.txt" 2>&1 || true
    grep -q 'tbf' "$dir/tc_qdisc_after.txt" || fail 'policy_engine did not apply network_qos tbf response'

    measure_rate "$dir" after 19092
    cp "$dir/after_rate.txt" "$dir/after_rate.raw" 2>/dev/null || true
    verify_rate_limit "$dir"

    TXN="$(extract_transaction_id)"
    verify_transaction_chain "$TXN"
    grep -q '"stage":"decision"' reports/events/policy_engine.jsonl || fail 'policy_engine decision stage missing'
    grep -q '"result":"applied"' reports/events/policy_engine.jsonl || fail 'policy_engine applied result missing'
    grep -q '"skill":"network_qos"' reports/events/network_policy.jsonl || fail 'network_qos policy response event missing'
    grep -q '"skill":"resource_control"' reports/events/resource_control.jsonl || fail 'resource_control policy response event missing'

    set +e
    wait "$AGENT_PID"
    local agent_rc="$?"
    set -e
    AGENT_PID=""
    [ "$agent_rc" -eq 0 ] || fail "agent exited non-zero, rc=$agent_rc"

    wait_for_file_value "$TARGET/cpu.max" "$OLD_CPU_MAX" || fail 'cpu.max was not restored'
    wait_for_file_value "$TARGET/memory.high" "$OLD_MEMORY_HIGH" || fail 'memory.high was not restored'
    tc qdisc show dev "$QOS_IFACE" > "$dir/tc_qdisc_rollback.txt" 2>&1 || true
    ! grep -q 'tbf' "$dir/tc_qdisc_rollback.txt" || fail 'tbf qdisc residue after rollback'
    grep -q '"result":"restored"' reports/events/policy_engine.jsonl || fail 'policy_engine restored result missing'
    verify_transaction_chain "$TXN"

    cp reports/events/security_policy.jsonl "$dir/security_policy_events.jsonl"
    cp reports/events/policy_engine.jsonl "$dir/policy_engine_events.jsonl"
    cp reports/events/network_policy.jsonl "$dir/network_policy_events.jsonl"
    cp reports/events/resource_control.jsonl "$dir/resource_control_events.jsonl"
    cp run/eulerpilot/action_journal.jsonl "$dir/action_journal.jsonl"

    cat > "$dir/summary.txt" <<EOF_SUMMARY
result=pass
case=success
transaction_id=$TXN
source_rule=burst_connect
resource_cpu_max=20000 100000
resource_memory_high=134217728
network_qos_rate=2mbit
old_cpu_max=$OLD_CPU_MAX
old_memory_high=$OLD_MEMORY_HIGH
EOF_SUMMARY

    cat > "$dir/report.md" <<EOF_REPORT
# Policy Engine Security -> Network + Resource Integration

- result: \`pass\`
- transaction_id: \`$TXN\`
- trigger: \`security_policy anomaly/burst_connect\`
- resource response: \`cpu.max=20000 100000\`, \`memory.high=134217728\`
- network response: \`network_qos tc/tbf rate=2mbit\`
- rollback: cgroup values restored and TBF qdisc removed

Evidence files in this directory include TC qdisc snapshots, rate probes, security/policy/network/resource events, and ActionJournal records.
EOF_REPORT

    cleanup_processes
    tc qdisc del dev "$QOS_IFACE" root >/dev/null 2>&1 || true
    rmdir "$TARGET" >/dev/null 2>&1 || true
    bash scripts/cleanup_network_qos_tc.sh "$QOS_IFACE" "$QOS_PEER" "$QOS_NETNS" >/dev/null 2>&1 || true
}

run_failure_case() {
    local dir="$RESULT_BASE/failure-rollback"
    mkdir -p "$dir" reports/events run/eulerpilot
    : > reports/events/security_policy.jsonl
    : > reports/events/policy_engine.jsonl
    : > reports/events/resource_control.jsonl
    : > reports/events/network_policy.jsonl
    : > run/eulerpilot/action_journal.jsonl

    cleanup_processes
    bash scripts/cleanup_network_qos_tc.sh "$QOS_IFACE" "$QOS_PEER" "$QOS_NETNS" >/dev/null 2>&1 || true
    create_lab_veth
    init_leaf_cgroup "$TARGET"
    OLD_CPU_MAX="$(cat "$TARGET/cpu.max")"
    OLD_MEMORY_HIGH="$(cat "$TARGET/memory.high")"
    write_agent_config "$dir"

    timeout 30s ./build/eulerpilot-agent \
        --config "$dir/agent.yaml" \
        --backend cgroup_v2 \
        --gate-mode normal \
        --active \
        --duration-s 10 \
        --interval-ms 500 \
        --jsonl \
        > "$dir/agent.log" 2>&1 &
    AGENT_PID="$!"
    sleep 1
    kill -0 "$AGENT_PID" 2>/dev/null || fail "failure-case agent exited early; see $dir/agent.log"
    wait_for_grep '"skill":"security_policy".*"operation":"start"' reports/events/security_policy.jsonl || \
        fail 'failure-case security_policy did not reach start state before trigger'
    wait_for_grep '"skill":"policy_engine".*"operation":"start"' reports/events/policy_engine.jsonl || \
        fail 'failure-case policy_engine did not reach start state before trigger'
    sleep 1

    # Delete the lab veth after probe/start. Resource action will apply first,
    # then network_qos action must fail and trigger transaction rollback.
    bash scripts/cleanup_network_qos_tc.sh "$QOS_IFACE" "$QOS_PEER" "$QOS_NETNS" >/dev/null 2>&1 || true
    trigger_burst_connect
    wait_for_grep '"result":"failed"' reports/events/policy_engine.jsonl || fail 'failure path did not produce failed policy event'
    wait_for_file_value "$TARGET/cpu.max" "$OLD_CPU_MAX" || fail 'failure path did not restore cpu.max'
    wait_for_file_value "$TARGET/memory.high" "$OLD_MEMORY_HIGH" || fail 'failure path did not restore memory.high'
    grep -q '"result":"restored"' reports/events/resource_control.jsonl || fail 'resource rollback event missing after network failure'

    set +e
    wait "$AGENT_PID"
    local agent_rc="$?"
    set -e
    AGENT_PID=""
    [ "$agent_rc" -eq 0 ] || fail "failure-case agent exited non-zero, rc=$agent_rc"

    cp reports/events/security_policy.jsonl "$dir/security_policy_events.jsonl"
    cp reports/events/policy_engine.jsonl "$dir/policy_engine_events.jsonl"
    cp reports/events/network_policy.jsonl "$dir/network_policy_events.jsonl"
    cp reports/events/resource_control.jsonl "$dir/resource_control_events.jsonl"
    cp run/eulerpilot/action_journal.jsonl "$dir/action_journal.jsonl"
    cat > "$dir/summary.txt" <<EOF_SUMMARY
result=pass
case=network_failure_rolls_back_resource
old_cpu_max=$OLD_CPU_MAX
old_memory_high=$OLD_MEMORY_HIGH
EOF_SUMMARY
}

[ "$(id -u)" -eq 0 ] || fail 'policy_engine security-network-resource test must run as root'
[[ "$REPEAT" =~ ^[0-9]+$ ]] || fail '--repeat must be numeric'
[ "$REPEAT" -ge 1 ] || fail '--repeat must be >= 1'
command -v ip >/dev/null 2>&1 || fail 'missing ip command'
command -v tc >/dev/null 2>&1 || fail 'missing tc command'
command -v python3 >/dev/null 2>&1 || fail 'missing python3 command'

mkdir -p "$RESULT_BASE"
make agent security-policy network-qos-tc
scripts/setup_cgroup_v2.sh > "$RESULT_BASE/setup.log" 2>&1

for iter in $(seq 1 "$REPEAT"); do
    info "policy_engine security-network-resource success iteration $iter/$REPEAT"
    run_success_case "$iter"
done

info 'policy_engine failure rollback case'
run_failure_case

cat > "$RESULT_BASE/summary.txt" <<EOF_SUMMARY
result=pass
repeat=$REPEAT
success_cases=$REPEAT
failure_rollback=pass
result_dir=$RESULT_BASE
EOF_SUMMARY

cat > "$RESULT_BASE/report.md" <<EOF_REPORT
# EulerPilot v3.1 Policy Engine Security -> Network + Resource Report

- result: \`pass\`
- repeat: \`$REPEAT\`
- success chain: \`security_policy burst_connect -> policy_engine -> resource_control + network_qos -> rollback\`
- failure chain: resource action applied, network action failed, resource rollback verified

Each iteration stores security, policy_engine, network_qos, resource_control and ActionJournal JSONL evidence plus TC qdisc and rate probe files.
EOF_REPORT

info "policy_engine security-network-resource result saved to $RESULT_BASE"
