#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR"

RESULT_DIR="${RESULT_DIR:-results/policy_engine/real-pod-security-network-resource-$(date +%Y%m%d-%H%M%S)}"
RESULT_ABS_DIR=""
KUBECTL_BIN="${EULERPILOT_KUBECTL_BIN:-kubectl}"
KUBECONFIG_PATH="${EULERPILOT_KUBECONFIG:-${KUBECONFIG:-}}"
POD_NAMESPACE="${EULERPILOT_POD_NAMESPACE:-eulerpilot-lab}"
POD_NAME="${EULERPILOT_POD_NAME:-eulerpilot-rc-pod}"
POD_CONTAINER_NAME="${EULERPILOT_POD_CONTAINER_NAME:-}"
POD_IMAGE="${EULERPILOT_POD_IMAGE:-localhost/eulerpilot-busybox:latest}"
POD_IMAGE_PULL_POLICY="${EULERPILOT_POD_IMAGE_PULL_POLICY:-IfNotPresent}"
ALLOW_K8S_CREATE="${EULERPILOT_ALLOW_K8S_CREATE:-0}"
CPU_MAX_VALUE="${EULERPILOT_POLICY_POD_CPU_MAX:-20000 100000}"
MEMORY_HIGH_VALUE="${EULERPILOT_POLICY_POD_MEMORY_HIGH:-134217728}"
QOS_RATE="${EULERPILOT_POLICY_POD_QOS_RATE:-2mbit}"
QOS_BURST="${EULERPILOT_POLICY_POD_QOS_BURST:-32kb}"
QOS_LATENCY="${EULERPILOT_POLICY_POD_QOS_LATENCY:-50ms}"
AGENT_DURATION_S="${EULERPILOT_POLICY_POD_AGENT_DURATION_S:-12}"
AGENT_TIMEOUT_S="${EULERPILOT_POLICY_POD_AGENT_TIMEOUT_S:-35}"

AGENT_PID=""
POD_CREATED=0
NS_CREATED=0
TARGET_CGROUP=""
HOST_VETH=""
POD_IP=""
POD_UID=""
OLD_CPU_MAX=""
OLD_MEMORY_HIGH=""
MEMORY_ACTION_ENABLED=1

mkdir -p "$RESULT_DIR" reports/events run/eulerpilot
: > "$RESULT_DIR/commands.log"
: > "$RESULT_DIR/summary.txt"

info() { printf '[INFO] %s\n' "$*"; }
fail() { printf '[FAIL] %s\n' "$*" >&2; exit 1; }

command_path() { command -v "$1" 2>/dev/null || true; }

log_cmd() {
    printf '$ %s\n' "$*" >> "$RESULT_DIR/commands.log"
    "$@" >> "$RESULT_DIR/commands.log" 2>&1
}

write_blocked() {
    local reason="$1"
    local next_action="$2"
    {
        printf 'result=blocked\n'
        printf 'reason=%s\n' "$reason"
        printf 'host=%s\n' "$(hostname 2>/dev/null || printf unknown)"
        printf 'date=%s\n' "$(date -Is)"
        printf 'kernel=%s\n' "$(uname -r)"
        printf 'kubectl_bin=%s\n' "$KUBECTL_BIN"
        printf 'kubectl_command=%s\n' "$(command_path "$KUBECTL_BIN")"
        printf 'kubeconfig=%s\n' "${KUBECONFIG_PATH:-default}"
        printf 'pod_namespace=%s\n' "$POD_NAMESPACE"
        printf 'pod_name=%s\n' "$POD_NAME"
        printf 'pod_container_name=%s\n' "$POD_CONTAINER_NAME"
        printf 'next_action=%s\n' "$next_action"
    } > "$RESULT_DIR/summary.txt"
}

write_report() {
    local result reason
    result="$(awk -F= '$1=="result"{print $2}' "$RESULT_DIR/summary.txt" 2>/dev/null || true)"
    reason="$(awk -F= '$1=="reason"{print $2}' "$RESULT_DIR/summary.txt" 2>/dev/null || true)"
    cat > "$RESULT_DIR/report.md" <<EOF_REPORT
# Policy Engine Real Pod Security -> Network + Resource

- result: \`${result:-unknown}\`
- reason: \`${reason:-unknown}\`
- host: \`$(hostname 2>/dev/null || printf unknown)\`
- kernel: \`$(uname -r)\`
- namespace: \`${POD_NAMESPACE}\`
- pod: \`${POD_NAME}\`
- cgroup: \`${TARGET_CGROUP:-unknown}\`
- host veth: \`${HOST_VETH:-unknown}\`

## Purpose

This test validates the real Kubernetes Pod target path for the second Policy Engine cross-skill chain. A single YAML target uses \`type: k8s_pod\`; Policy Engine resolves it to the Pod cgroup for \`resource_control\` actions and to the Pod host veth for \`network_qos\` actions.

## Evidence

- \`summary.txt\`
- \`commands.log\`
- \`agent.yaml\`, \`skills.yaml\`
- \`resolve_policy_pod_target.txt\`
- \`tc_qdisc_before.txt\`, \`tc_qdisc_after.txt\`, \`tc_qdisc_rollback.txt\`
- \`security_policy_events.jsonl\`
- \`policy_engine_events.jsonl\`
- \`resource_control_events.jsonl\`
- \`network_policy_events.jsonl\`
- \`action_journal.jsonl\`
EOF_REPORT
}

cleanup() {
    set +e
    if [ -n "$AGENT_PID" ] && kill -0 "$AGENT_PID" 2>/dev/null; then
        kill "$AGENT_PID" 2>/dev/null || true
        wait "$AGENT_PID" 2>/dev/null || true
    fi
    if [ -n "$HOST_VETH" ]; then
        tc qdisc del dev "$HOST_VETH" root >/dev/null 2>&1 || true
        tc qdisc del dev "$HOST_VETH" clsact >/dev/null 2>&1 || true
    fi
    if [ -n "$TARGET_CGROUP" ]; then
        if [ -n "$OLD_CPU_MAX" ] && [ -w "$TARGET_CGROUP/cpu.max" ]; then
            printf '%s\n' "$OLD_CPU_MAX" > "$TARGET_CGROUP/cpu.max" 2>/dev/null || true
        fi
        if [ -n "$OLD_MEMORY_HIGH" ] && [ -w "$TARGET_CGROUP/memory.high" ]; then
            printf '%s\n' "$OLD_MEMORY_HIGH" > "$TARGET_CGROUP/memory.high" 2>/dev/null || true
        fi
    fi
    scripts/rollback.sh >> "$RESULT_DIR/commands.log" 2>&1 || true
    if [ "$POD_CREATED" = "1" ]; then
        "$RESULT_ABS_DIR/kubectl-wrapper" -n "$POD_NAMESPACE" delete pod "$POD_NAME" \
            --ignore-not-found=true >> "$RESULT_DIR/commands.log" 2>&1 || true
    fi
    if [ "$NS_CREATED" = "1" ]; then
        "$RESULT_ABS_DIR/kubectl-wrapper" delete namespace "$POD_NAMESPACE" \
            --ignore-not-found=true >> "$RESULT_DIR/commands.log" 2>&1 || true
    fi
}
trap cleanup EXIT

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

wait_for_qdisc() {
    local deadline=$((SECONDS + 25))
    while [ "$SECONDS" -lt "$deadline" ]; do
        if tc qdisc show dev "$HOST_VETH" 2>/dev/null | grep -q 'tbf'; then
            return 0
        fi
        sleep 0.2
    done
    return 1
}

write_wrappers() {
    RESULT_ABS_DIR="$(cd "$RESULT_DIR" && pwd)"
    cat > "$RESULT_ABS_DIR/kubectl-wrapper" <<EOF_KUBECTL
#!/bin/sh
if [ -n "${KUBECONFIG_PATH}" ]; then
    exec "${KUBECTL_BIN}" --kubeconfig "${KUBECONFIG_PATH}" "\$@"
fi
exec "${KUBECTL_BIN}" "\$@"
EOF_KUBECTL
    chmod +x "$RESULT_ABS_DIR/kubectl-wrapper"

    cat > "$RESULT_ABS_DIR/crictl-wrapper" <<'EOF_CRICTL'
#!/bin/sh
if command -v k3s >/dev/null 2>&1; then
    exec k3s crictl "$@"
fi
exec crictl "$@"
EOF_CRICTL
    chmod +x "$RESULT_ABS_DIR/crictl-wrapper"
}

ensure_lab_pod() {
    if ! timeout 8s "$RESULT_ABS_DIR/kubectl-wrapper" get namespace "$POD_NAMESPACE" \
        > "$RESULT_DIR/kubectl_get_namespace.txt" 2>&1; then
        if [ "$ALLOW_K8S_CREATE" != "1" ]; then
            write_blocked "lab-namespace-missing" \
                "create-eulerpilot-lab-namespace-or-set-EULERPILOT_ALLOW_K8S_CREATE=1"
            return 1
        fi
        log_cmd "$RESULT_ABS_DIR/kubectl-wrapper" create namespace "$POD_NAMESPACE"
        NS_CREATED=1
    fi

    if ! timeout 8s "$RESULT_ABS_DIR/kubectl-wrapper" -n "$POD_NAMESPACE" get pod "$POD_NAME" \
        > "$RESULT_DIR/kubectl_get_pod.txt" 2>&1; then
        if [ "$ALLOW_K8S_CREATE" != "1" ]; then
            write_blocked "lab-pod-missing" \
                "create-demo-pod-or-set-EULERPILOT_ALLOW_K8S_CREATE=1"
            return 1
        fi
        log_cmd "$RESULT_ABS_DIR/kubectl-wrapper" -n "$POD_NAMESPACE" run "$POD_NAME" \
            --image="$POD_IMAGE" --image-pull-policy="$POD_IMAGE_PULL_POLICY" \
            --restart=Never --command -- sh -c 'yes >/dev/null'
        POD_CREATED=1
    fi

    if ! timeout 70s "$RESULT_ABS_DIR/kubectl-wrapper" -n "$POD_NAMESPACE" wait \
        --for=condition=Ready "pod/$POD_NAME" --timeout=60s \
        > "$RESULT_DIR/kubectl_wait_pod.txt" 2>&1; then
        write_blocked "lab-pod-not-ready" "check-pod-image-runtime-and-cluster-events"
        return 1
    fi

    POD_UID="$($RESULT_ABS_DIR/kubectl-wrapper -n "$POD_NAMESPACE" get pod "$POD_NAME" \
        -o 'jsonpath={.metadata.uid}' | tr -d '[:space:]')"
    POD_IP="$($RESULT_ABS_DIR/kubectl-wrapper -n "$POD_NAMESPACE" get pod "$POD_NAME" \
        -o 'jsonpath={.status.podIP}' | tr -d '[:space:]')"
    if [ -z "$POD_UID" ] || [ -z "$POD_IP" ]; then
        write_blocked "pod-identity-query-failed" "check-kubectl-access-to-demo-pod"
        return 1
    fi
}

resolve_policy_pod_target() {
    cat > "$RESULT_DIR/resolve_policy_pod_target.cpp" <<'CPP'
#include "target_resolver.hpp"
#include <iostream>

int main(int argc, char **argv) {
    if (argc != 6) {
        std::cerr << "usage: resolve <namespace> <pod> <container-name> <kubectl> <crictl>\n";
        return 2;
    }
    eulerpilot::K8sPodTargetSpec spec;
    spec.name = "lab_pod";
    spec.pod_namespace = argv[1];
    spec.pod_name = argv[2];
    spec.container_name = argv[3];
    eulerpilot::TargetResolverOptions options;
    options.kubectl_path = argv[4];
    options.crictl_path = argv[5];
    options.require_runtime_socket = false;
    options.lab_namespace = "eulerpilot-lab";

    const auto cgroup = eulerpilot::resolve_k8s_pod_cgroup_target(spec, options);
    const auto netdev = eulerpilot::resolve_k8s_pod_target(spec, options);
    std::cout << "cgroup_resolved=" << (cgroup.resolved ? "1" : "0") << "\n";
    std::cout << "cgroup_reason=" << cgroup.reason << "\n";
    std::cout << "cgroup_path=" << cgroup.cgroup_path << "\n";
    std::cout << "cgroup_id=" << cgroup.cgroup_id << "\n";
    std::cout << "pod_uid=" << cgroup.pod_uid << "\n";
    std::cout << "netdev_resolved=" << (netdev.resolved ? "1" : "0") << "\n";
    std::cout << "netdev_reason=" << netdev.reason << "\n";
    std::cout << "ifname=" << netdev.ifname << "\n";
    std::cout << "ifindex=" << netdev.ifindex << "\n";
    std::cout << "pid=" << netdev.pid << "\n";
    std::cout << "container_id=" << netdev.container_id << "\n";
    std::cout << "netns_path=" << netdev.netns_path << "\n";
    return cgroup.resolved && netdev.resolved ? 0 : 1;
}
CPP
    g++ -std=c++17 -Wall -Wextra -Iagent/include \
        agent/src/target_resolver.cpp "$RESULT_DIR/resolve_policy_pod_target.cpp" \
        -o "$RESULT_DIR/resolve_policy_pod_target"
    if ! "$RESULT_DIR/resolve_policy_pod_target" "$POD_NAMESPACE" "$POD_NAME" \
        "$POD_CONTAINER_NAME" "$RESULT_ABS_DIR/kubectl-wrapper" \
        "$RESULT_ABS_DIR/crictl-wrapper" > "$RESULT_DIR/resolve_policy_pod_target.txt" 2>&1; then
        write_blocked "pod-target-resolve-failed" "check-kubectl-cri-cgroup-and-pod-netns"
        return 1
    fi
    TARGET_CGROUP="$(awk -F= '$1=="cgroup_path"{print $2}' "$RESULT_DIR/resolve_policy_pod_target.txt")"
    HOST_VETH="$(awk -F= '$1=="ifname"{print $2}' "$RESULT_DIR/resolve_policy_pod_target.txt")"
    if [ -z "$TARGET_CGROUP" ] || [ ! -d "$TARGET_CGROUP" ]; then
        write_blocked "pod-cgroup-not-found" "check-cgroup-v2-layout-for-pod-uid"
        return 1
    fi
    if [ -z "$HOST_VETH" ] || ! ip link show dev "$HOST_VETH" >/dev/null 2>&1; then
        write_blocked "pod-veth-not-found" "check-pod-host-veth-resolution"
        return 1
    fi
    case "$HOST_VETH" in
        eth*|ens*|eno*|wlan*|bond*|br*|cni*|flannel*)
            write_blocked "pod-veth-denied-host-netdev" "refuse-to-operate-production-or-cni-netdev"
            return 1
            ;;
    esac
}

memory_high_is_allowed() {
    local max_value
    max_value="$(cat "$TARGET_CGROUP/memory.max" 2>/dev/null || printf max)"
    if [ "$max_value" = "max" ]; then
        return 0
    fi
    python3 - "$max_value" "$MEMORY_HIGH_VALUE" <<'PY'
import sys
try:
    max_value = int(sys.argv[1])
    high = int(sys.argv[2])
except ValueError:
    raise SystemExit(0)
raise SystemExit(0 if max_value > high else 1)
PY
}

write_agent_config() {
    local container_name_yaml=""
    local memory_action_yaml=""
    if [ -n "$POD_CONTAINER_NAME" ]; then
        container_name_yaml="        container_name: $POD_CONTAINER_NAME"
    fi
    if [ "$MEMORY_ACTION_ENABLED" = "1" ]; then
        memory_action_yaml="      - name: cap_real_pod_memory
        target_ref: lab_pod
        file: memory.high
        value: '$MEMORY_HIGH_VALUE'"
    fi

    cat > "$RESULT_DIR/agent.yaml" <<YAML
agent:
  name: EulerPilot
  mode: active
skills_config_path: $RESULT_ABS_DIR/skills.yaml
scheduler:
  type: cgroup_v2
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
      lab_pod:
        type: k8s_pod
        namespace: $POD_NAMESPACE
        pod_name: $POD_NAME
$container_name_yaml
        kubectl_path: $RESULT_ABS_DIR/kubectl-wrapper
        crictl_path: $RESULT_ABS_DIR/crictl-wrapper
        lab_namespace: eulerpilot-lab
        require_runtime_socket: false
    rules:
      - name: policy_engine_real_pod_qos_guard
        hook: tc_egress
        target_ref: lab_pod
        protocol: any
        dst_port: '0'
        rate: $QOS_RATE
        burst: $QOS_BURST
        latency: $QOS_LATENCY
        action: limit
- name: security_policy
  kind: runtime
  enabled: true
  config:
    mode: audit
    targets:
      lab_connect:
        type: path
        dst_ip: 127.0.0.1
        dst_port: '65000'
    rules:
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
- name: policy_engine
  kind: runtime
  enabled: true
  config:
    mode: enforce
    policy_id: security_network_resource_real_pod_response
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
      lab_pod:
        type: k8s_pod
        namespace: $POD_NAMESPACE
        pod_name: $POD_NAME
$container_name_yaml
        kubectl_path: $RESULT_ABS_DIR/kubectl-wrapper
        crictl_path: $RESULT_ABS_DIR/crictl-wrapper
        lab_namespace: eulerpilot-lab
        require_runtime_socket: false
    actions:
      - name: throttle_real_pod_cpu
        target_ref: lab_pod
        file: cpu.max
        value: '$CPU_MAX_VALUE'
$memory_action_yaml
      - name: limit_real_pod_veth
        target_ref: lab_pod
        file: network_qos.rate
        value: $QOS_RATE
        burst: $QOS_BURST
        latency: $QOS_LATENCY
YAML
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

extract_transaction_id() {
    python3 - <<'PY'
import json
from pathlib import Path
path = Path('reports/events/policy_engine.jsonl')
if not path.exists():
    raise SystemExit(0)
for line in path.read_text().splitlines():
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

[ "$(id -u)" -eq 0 ] || fail 'real Pod policy_engine test must run as root'
command -v python3 >/dev/null 2>&1 || fail 'missing python3 command'
command -v g++ >/dev/null 2>&1 || fail 'missing g++ command'
command -v ip >/dev/null 2>&1 || fail 'missing ip command'
command -v tc >/dev/null 2>&1 || fail 'missing tc command'

if ! command -v "$KUBECTL_BIN" >/dev/null 2>&1; then
    write_blocked "missing-kubectl" "install-kubectl-and-provide-eulerpilot-lab-demo-pod"
    write_report
    info "result directory: $RESULT_DIR"
    exit 0
fi

write_wrappers
if ! ensure_lab_pod; then
    write_report
    info "result directory: $RESULT_DIR"
    exit 0
fi
if ! resolve_policy_pod_target; then
    write_report
    info "result directory: $RESULT_DIR"
    exit 0
fi

if [ ! -w "$TARGET_CGROUP/cpu.max" ]; then
    write_blocked "pod-cpu-max-not-writable" "enable-cgroup-v2-cpu-controller-for-kubelet-pod"
    write_report
    info "result directory: $RESULT_DIR"
    exit 0
fi
if [ ! -w "$TARGET_CGROUP/memory.high" ]; then
    MEMORY_ACTION_ENABLED=0
elif ! memory_high_is_allowed; then
    MEMORY_ACTION_ENABLED=0
    printf 'memory_high_skip_reason=memory.max-below-target\n' >> "$RESULT_DIR/summary.txt"
fi

if [ ! -x ./build/eulerpilot-agent ] || [ ! -f ./build/security_policy_demo.bpf.o ]; then
    log_cmd make agent security-policy-demo network-qos-tc
fi
if [ ! -x ./build/eulerpilot-agent ]; then
    write_blocked "agent-build-missing" "run-make-agent"
    write_report
    info "result directory: $RESULT_DIR"
    exit 0
fi

: > reports/events/security_policy.jsonl
: > reports/events/policy_engine.jsonl
: > reports/events/resource_control.jsonl
: > reports/events/network_policy.jsonl
: > run/eulerpilot/action_journal.jsonl

OLD_CPU_MAX="$(cat "$TARGET_CGROUP/cpu.max")"
OLD_MEMORY_HIGH="$(cat "$TARGET_CGROUP/memory.high" 2>/dev/null || true)"
write_agent_config

./build/eulerpilot-agent --validate-config "$RESULT_DIR/agent.yaml" > "$RESULT_DIR/validate_config.txt" 2>&1 || \
    fail "validate-config failed; see $RESULT_DIR/validate_config.txt"

tc qdisc show dev "$HOST_VETH" > "$RESULT_DIR/tc_qdisc_before.txt" 2>&1 || true

timeout "${AGENT_TIMEOUT_S}s" ./build/eulerpilot-agent \
    --config "$RESULT_DIR/agent.yaml" \
    --backend cgroup_v2 \
    --gate-mode normal \
    --active \
    --duration-s "$AGENT_DURATION_S" \
    --interval-ms 500 \
    --jsonl \
    > "$RESULT_DIR/agent.log" 2>&1 &
AGENT_PID="$!"

sleep 1
kill -0 "$AGENT_PID" 2>/dev/null || fail "agent exited early; see $RESULT_DIR/agent.log"
wait_for_grep '"skill":"security_policy".*"operation":"start"' reports/events/security_policy.jsonl || \
    fail 'security_policy did not start before trigger'
wait_for_grep '"skill":"policy_engine".*"operation":"start"' reports/events/policy_engine.jsonl || \
    fail 'policy_engine did not start before trigger'

trigger_burst_connect
wait_for_file_value "$TARGET_CGROUP/cpu.max" "$CPU_MAX_VALUE" || \
    fail 'policy_engine did not apply real Pod cpu.max response'
if [ "$MEMORY_ACTION_ENABLED" = "1" ]; then
    wait_for_file_value "$TARGET_CGROUP/memory.high" "$MEMORY_HIGH_VALUE" || \
        fail 'policy_engine did not apply real Pod memory.high response'
fi
wait_for_qdisc || fail 'policy_engine did not apply TBF on real Pod host veth'
tc qdisc show dev "$HOST_VETH" > "$RESULT_DIR/tc_qdisc_after.txt" 2>&1 || true
ping -c 5 -W 1 "$POD_IP" > "$RESULT_DIR/ping_pod.txt" 2>&1 || true
wait_for_grep '"skill":"network_qos".*"result":"applied"' reports/events/network_policy.jsonl || \
    fail 'network_qos applied event missing'
wait_for_grep '"skill":"resource_control".*"result":"applied"' reports/events/resource_control.jsonl || \
    fail 'resource_control applied event missing'

TXN="$(extract_transaction_id)"
verify_transaction_chain "$TXN"
grep -q '"target_type":"k8s_pod"' reports/events/policy_engine.jsonl || fail 'policy_engine event missing k8s_pod target_type'
grep -q '"resolved_target_type":"cgroup"' reports/events/policy_engine.jsonl || fail 'policy_engine event missing cgroup resolved target'
grep -q '"resolved_target_type":"netdev"' reports/events/policy_engine.jsonl || fail 'policy_engine event missing netdev resolved target'

set +e
wait "$AGENT_PID"
AGENT_RC="$?"
set -e
AGENT_PID=""
[ "$AGENT_RC" -eq 0 ] || fail "agent exited non-zero rc=$AGENT_RC"

wait_for_file_value "$TARGET_CGROUP/cpu.max" "$OLD_CPU_MAX" || fail 'real Pod cpu.max was not restored'
if [ "$MEMORY_ACTION_ENABLED" = "1" ]; then
    wait_for_file_value "$TARGET_CGROUP/memory.high" "$OLD_MEMORY_HIGH" || fail 'real Pod memory.high was not restored'
fi
tc qdisc show dev "$HOST_VETH" > "$RESULT_DIR/tc_qdisc_rollback.txt" 2>&1 || true
! grep -q 'tbf' "$RESULT_DIR/tc_qdisc_rollback.txt" || fail 'TBF qdisc residue after rollback'
grep -q '"result":"restored"' reports/events/policy_engine.jsonl || fail 'policy_engine restored result missing'
verify_transaction_chain "$TXN"

cp reports/events/security_policy.jsonl "$RESULT_DIR/security_policy_events.jsonl"
cp reports/events/policy_engine.jsonl "$RESULT_DIR/policy_engine_events.jsonl"
cp reports/events/network_policy.jsonl "$RESULT_DIR/network_policy_events.jsonl"
cp reports/events/resource_control.jsonl "$RESULT_DIR/resource_control_events.jsonl"
cp run/eulerpilot/action_journal.jsonl "$RESULT_DIR/action_journal.jsonl"

{
    printf 'result=pass\n'
    printf 'reason=real-pod-security-network-resource-applied-and-restored\n'
    printf 'host=%s\n' "$(hostname 2>/dev/null || printf unknown)"
    printf 'date=%s\n' "$(date -Is)"
    printf 'kernel=%s\n' "$(uname -r)"
    printf 'transaction_id=%s\n' "$TXN"
    printf 'policy_id=security_network_resource_real_pod_response\n'
    printf 'source_rule=burst_connect\n'
    printf 'pod_namespace=%s\n' "$POD_NAMESPACE"
    printf 'pod_name=%s\n' "$POD_NAME"
    printf 'pod_uid=%s\n' "$POD_UID"
    printf 'pod_ip=%s\n' "$POD_IP"
    printf 'target_ref=lab_pod\n'
    printf 'target_type=k8s_pod\n'
    printf 'target_cgroup=%s\n' "$TARGET_CGROUP"
    printf 'host_veth=%s\n' "$HOST_VETH"
    printf 'cpu_max_value=%s\n' "$CPU_MAX_VALUE"
    printf 'memory_action_enabled=%s\n' "$MEMORY_ACTION_ENABLED"
    printf 'memory_high_value=%s\n' "$MEMORY_HIGH_VALUE"
    printf 'network_qos_rate=%s\n' "$QOS_RATE"
    printf 'old_cpu_max=%s\n' "$OLD_CPU_MAX"
    printf 'old_memory_high=%s\n' "$OLD_MEMORY_HIGH"
} > "$RESULT_DIR/summary.txt"

write_report
info "result directory: $RESULT_DIR"