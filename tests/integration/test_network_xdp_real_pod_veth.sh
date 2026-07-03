#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR"

RESULT_DIR="${RESULT_DIR:-results/network_policy/real-pod-veth-xdp-$(date +%Y%m%d-%H%M%S)}"
RESULT_ABS_DIR=""
KUBECTL_BIN="${EULERPILOT_KUBECTL_BIN:-kubectl}"
KUBECONFIG_PATH="${EULERPILOT_KUBECONFIG:-${KUBECONFIG:-}}"
POD_NAMESPACE="${EULERPILOT_POD_NAMESPACE:-eulerpilot-lab}"
POD_NAME="${EULERPILOT_POD_NAME:-eulerpilot-rc-pod}"
POD_CONTAINER_NAME="${EULERPILOT_POD_CONTAINER_NAME:-}"
POD_IMAGE="${EULERPILOT_POD_IMAGE:-localhost/eulerpilot-busybox:latest}"
POD_IMAGE_PULL_POLICY="${EULERPILOT_POD_IMAGE_PULL_POLICY:-IfNotPresent}"
ALLOW_K8S_CREATE="${EULERPILOT_ALLOW_K8S_CREATE:-0}"
AGENT_AUDIT_DURATION_S="${EULERPILOT_REAL_POD_XDP_AUDIT_DURATION_S:-1}"
AGENT_ENFORCE_DURATION_S="${EULERPILOT_REAL_POD_XDP_ENFORCE_DURATION_S:-8}"
AGENT_TIMEOUT_S="${EULERPILOT_REAL_POD_XDP_AGENT_TIMEOUT_S:-25}"
XDP_TCP_PORT="${EULERPILOT_REAL_POD_XDP_TCP_PORT:-19092}"
XDP_UDP_PORT="${EULERPILOT_REAL_POD_XDP_UDP_PORT:-19093}"
XDP_UDP_TUPLE_PORT="${EULERPILOT_REAL_POD_XDP_UDP_TUPLE_PORT:-19094}"
XDP_UDP_TUPLE_SRC_PORT="${EULERPILOT_REAL_POD_XDP_UDP_TUPLE_SRC_PORT:-39094}"

AGENT_PID=""
POD_CREATED=0
NS_CREATED=0
HOST_VETH=""
HOST_IP=""
HOST_BRIDGE=""
POD_PID=""
POD_IP=""
TRAFFIC_TARGET_IP=""

mkdir -p "$RESULT_DIR" reports/events run/eulerpilot
: > "$RESULT_DIR/commands.log"
: > "$RESULT_DIR/summary.txt"

info() { printf '[INFO] %s\n' "$*"; }
fail() { printf '[FAIL] %s\n' "$*" >&2; exit 1; }

command_path() {
    command -v "$1" 2>/dev/null || true
}

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
        printf 'pod_image=%s\n' "$POD_IMAGE"
        printf 'pod_image_pull_policy=%s\n' "$POD_IMAGE_PULL_POLICY"
        printf 'next_action=%s\n' "$next_action"
    } > "$RESULT_DIR/summary.txt"
}

write_report() {
    local result reason xdp_drop_count xdp_tcp_drop_count xdp_udp_drop_count xdp_udp_tuple_drop_count
    result="$(awk -F= '$1=="result"{print $2}' "$RESULT_DIR/summary.txt" 2>/dev/null || true)"
    reason="$(awk -F= '$1=="reason"{print $2}' "$RESULT_DIR/summary.txt" 2>/dev/null || true)"
    xdp_drop_count="$(awk -F= '$1=="xdp_drop_count"{print $2}' "$RESULT_DIR/summary.txt" 2>/dev/null || true)"
    xdp_tcp_drop_count="$(awk -F= '$1=="xdp_tcp_drop_count"{print $2}' "$RESULT_DIR/summary.txt" 2>/dev/null || true)"
    xdp_udp_drop_count="$(awk -F= '$1=="xdp_udp_drop_count"{print $2}' "$RESULT_DIR/summary.txt" 2>/dev/null || true)"
    xdp_udp_tuple_drop_count="$(awk -F= '$1=="xdp_udp_tuple_drop_count"{print $2}' "$RESULT_DIR/summary.txt" 2>/dev/null || true)"
    cat > "$RESULT_DIR/report.md" <<EOF_REPORT
# Network XDP Real Pod veth Target

- result: \`${result:-unknown}\`
- reason: \`${reason:-unknown}\`
- host: \`$(hostname 2>/dev/null || printf unknown)\`
- kernel: \`$(uname -r)\`
- namespace: \`${POD_NAMESPACE}\`
- pod: \`${POD_NAME}\`
- host veth: \`${HOST_VETH:-unknown}\`
- xdp drop count: \`${xdp_drop_count:-unknown}\`
- xdp TCP drop count: \`${xdp_tcp_drop_count:-unknown}\`
- xdp UDP drop count: \`${xdp_udp_drop_count:-unknown}\`
- xdp UDP tuple drop count: \`${xdp_udp_tuple_drop_count:-unknown}\`

## Purpose

This test validates the real Kubernetes Pod host-veth path for \`network_xdp.target_ref\`.
It resolves \`type: k8s_pod\` in the lab namespace, attaches generic XDP to the resolved
host veth, sends Pod-to-host traffic, verifies XDP drop evidence, and checks rollback
detaches the XDP program.

The script only allows the \`eulerpilot-lab\` namespace by default. It creates the
namespace or Pod only when \`EULERPILOT_ALLOW_K8S_CREATE=1\` is set.

## Artifacts

- \`summary.txt\`
- \`commands.log\`
- \`agent.audit.yaml\`, \`skills.audit.yaml\`
- \`agent.enforce.yaml\`, \`skills.enforce.yaml\`
- \`resolve_pod_veth.txt\`
- \`xdp_link_before.txt\`, \`xdp_link_audit.txt\`, \`xdp_link_enforce.txt\`, \`xdp_link_rollback.txt\`
- \`baseline_ping.txt\`, \`enforce_ping_drop.txt\`, \`rollback_ping.txt\`
- \`enforce_tcp_drop.txt\`, \`enforce_udp_drop.txt\`, \`enforce_udp_tuple_drop.txt\`, \`xdp_rule_stats.txt\`
- \`network_policy.jsonl\`
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
        ip link set dev "$HOST_VETH" xdpgeneric off >/dev/null 2>&1 || true
        ip link set dev "$HOST_VETH" xdp off >/dev/null 2>&1 || true
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
    if [ "$POD_NAMESPACE" != "eulerpilot-lab" ] && [ "${EULERPILOT_ALLOW_NON_LAB_PODS:-0}" != "1" ]; then
        write_blocked "non-lab-namespace-denied" "use-eulerpilot-lab-or-set-explicit-non-lab-override"
        return 1
    fi

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
            --restart=Never --command -- sh -c 'sleep 3600'
        POD_CREATED=1
    fi

    if ! timeout 70s "$RESULT_ABS_DIR/kubectl-wrapper" -n "$POD_NAMESPACE" wait \
        --for=condition=Ready "pod/$POD_NAME" --timeout=60s \
        > "$RESULT_DIR/kubectl_wait_pod.txt" 2>&1; then
        write_blocked "lab-pod-not-ready" "check-pod-image-runtime-and-cluster-events"
        return 1
    fi

    HOST_IP="$($RESULT_ABS_DIR/kubectl-wrapper -n "$POD_NAMESPACE" get pod "$POD_NAME" \
        -o 'jsonpath={.status.hostIP}' | tr -d '[:space:]')"
    if [ -z "$HOST_IP" ]; then
        write_blocked "host-ip-query-failed" "check-kubectl-access-to-demo-pod"
        return 1
    fi
    POD_IP="$($RESULT_ABS_DIR/kubectl-wrapper -n "$POD_NAMESPACE" get pod "$POD_NAME" \
        -o 'jsonpath={.status.podIP}' | tr -d '[:space:]')"
    if [ -z "$POD_IP" ]; then
        write_blocked "pod-ip-query-failed" "check-kubectl-access-to-demo-pod"
        return 1
    fi
}

resolve_host_veth() {
    cat > "$RESULT_DIR/resolve_pod_veth.cpp" <<'CPP'
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
    const auto target = eulerpilot::resolve_k8s_pod_target(spec, options);
    std::cout << "resolved=" << (target.resolved ? "1" : "0") << "\n";
    std::cout << "reason=" << target.reason << "\n";
    std::cout << "ifname=" << target.ifname << "\n";
    std::cout << "ifindex=" << target.ifindex << "\n";
    std::cout << "pid=" << target.pid << "\n";
    std::cout << "pod_uid=" << target.pod_uid << "\n";
    std::cout << "container_id=" << target.container_id << "\n";
    std::cout << "netns_path=" << target.netns_path << "\n";
    std::cout << "cgroup_path=" << target.cgroup_path << "\n";
    return target.resolved ? 0 : 1;
}
CPP
    g++ -std=c++17 -Wall -Wextra -Iagent/include \
        agent/src/target_resolver.cpp "$RESULT_DIR/resolve_pod_veth.cpp" \
        -o "$RESULT_DIR/resolve_pod_veth"
    if ! "$RESULT_DIR/resolve_pod_veth" "$POD_NAMESPACE" "$POD_NAME" \
        "$POD_CONTAINER_NAME" "$RESULT_ABS_DIR/kubectl-wrapper" \
        "$RESULT_ABS_DIR/crictl-wrapper" > "$RESULT_DIR/resolve_pod_veth.txt" 2>&1; then
        write_blocked "pod-veth-resolve-failed" "check-cri-access-and-pod-network-namespace"
        cat "$RESULT_DIR/resolve_pod_veth.txt" >> "$RESULT_DIR/summary.txt"
        return 1
    fi
    HOST_VETH="$(awk -F= '$1=="ifname"{print $2}' "$RESULT_DIR/resolve_pod_veth.txt")"
    POD_PID="$(awk -F= '$1=="pid"{print $2}' "$RESULT_DIR/resolve_pod_veth.txt")"
    if [ -z "$HOST_VETH" ]; then
        write_blocked "pod-veth-ifname-empty" "check-target-resolver-output"
        return 1
    fi
    if [ -z "$POD_PID" ] || [ "$POD_PID" = "0" ]; then
        write_blocked "pod-netns-pid-empty" "check-target-resolver-output"
        return 1
    fi
    case "$HOST_VETH" in
        eth*|ens*|eno*|wlan*|bond*|br*|cni*|flannel*)
            write_blocked "pod-veth-denied-host-netdev" "refuse-to-operate-production-or-cni-netdev"
            printf 'host_veth=%s\n' "$HOST_VETH" >> "$RESULT_DIR/summary.txt"
            return 1
            ;;
    esac

    HOST_BRIDGE="$(ip -o link show "$HOST_VETH" 2>/dev/null |
        sed -n 's/.* master \([^ ]*\).*/\1/p' | head -n 1)"
    if [ -n "$HOST_BRIDGE" ]; then
        TRAFFIC_TARGET_IP="$(ip -4 -o addr show dev "$HOST_BRIDGE" 2>/dev/null |
            awk '{split($4, a, "/"); print a[1]; exit}')"
    fi
    if [ -z "$TRAFFIC_TARGET_IP" ]; then
        TRAFFIC_TARGET_IP="$HOST_IP"
    fi
    if [ -z "$TRAFFIC_TARGET_IP" ]; then
        write_blocked "traffic-target-ip-empty" "check-node-cni-bridge-or-pod-host-ip"
        return 1
    fi
}

write_agent_config() {
    local mode="$1"
    local suffix="$2"
    local container_name_yaml=""
    if [ -n "$POD_CONTAINER_NAME" ]; then
        container_name_yaml="        container_name: $POD_CONTAINER_NAME"
    fi

    cat > "$RESULT_DIR/agent.$suffix.yaml" <<YAML
agent:
  name: EulerPilot
  mode: active
skills_config_path: $RESULT_ABS_DIR/skills.$suffix.yaml
scheduler:
  type: cgroup_v2
exporter:
  prometheus:
    enabled: false
YAML

    cat > "$RESULT_DIR/skills.$suffix.yaml" <<YAML
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
          enabled: false
      memory:
        enabled: false
        reclaim:
          enabled: false
      io:
        enabled: false
        weight:
          enabled: false
        max:
          enabled: false
    targets:
      background_cgroup:
        type: cgroup
        cgroup_path: /sys/fs/cgroup/eulerpilot/background
    profiles:
      background:
        target_ref: background_cgroup
        normal:
          cpu_max: max
          memory_high: max
        pressure:
          target_ref: background_cgroup
          cpu_max: max
          memory_high: max
- name: psi_gate
  kind: runtime
  enabled: false
  config: {}
- name: network_xdp
  kind: runtime
  enabled: true
  config:
    mode: $mode
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
      - name: drop_icmp_real_pod
        hook: xdp
        mode: $mode
        target_ref: lab_pod
        protocol: icmp
        dst_port: '0'
        action: drop
      - name: drop_tcp_real_pod
        hook: xdp
        mode: $mode
        target_ref: lab_pod
        protocol: tcp
        dst_port: '$XDP_TCP_PORT'
        action: drop
      - name: drop_udp_real_pod
        hook: xdp
        mode: $mode
        target_ref: lab_pod
        protocol: udp
        dst_port: '$XDP_UDP_PORT'
        action: drop
      - name: drop_udp_tuple_real_pod
        hook: xdp
        mode: $mode
        target_ref: lab_pod
        protocol: udp
        src_ip: $POD_IP
        dst_ip: $TRAFFIC_TARGET_IP
        src_port: '$XDP_UDP_TUPLE_SRC_PORT'
        dst_port: '$XDP_UDP_TUPLE_PORT'
        action: drop
YAML
}

has_xdp_attached() {
    ip -d link show "$HOST_VETH" 2>/dev/null |
        grep -Eqi 'prog/xdp|xdpgeneric|xdpdrv|xdpoffload|xdp/id'
}

wait_for_xdp_attach() {
    local deadline=$((SECONDS + 20))
    while [ "$SECONDS" -lt "$deadline" ]; do
        if has_xdp_attached; then
            return 0
        fi
        sleep 0.2
    done
    return 1
}

pod_exec() {
    if [ -n "$POD_CONTAINER_NAME" ]; then
        "$RESULT_ABS_DIR/kubectl-wrapper" -n "$POD_NAMESPACE" exec "$POD_NAME" \
            -c "$POD_CONTAINER_NAME" -- "$@"
    else
        "$RESULT_ABS_DIR/kubectl-wrapper" -n "$POD_NAMESPACE" exec "$POD_NAME" -- "$@"
    fi
}

pod_netns_exec() {
    if [ -n "$POD_PID" ] && command -v nsenter >/dev/null 2>&1; then
        nsenter -t "$POD_PID" -n "$@"
        return
    fi
    pod_exec "$@"
}

ping_host_from_pod() {
    pod_netns_exec ping -c 1 -W 1 "$TRAFFIC_TARGET_IP"
}

tcp_probe_from_pod() {
    if [ -n "$POD_PID" ] && command -v nsenter >/dev/null 2>&1 &&
        command -v python3 >/dev/null 2>&1; then
        nsenter -t "$POD_PID" -n python3 - "$TRAFFIC_TARGET_IP" "$XDP_TCP_PORT" <<'PY'
import socket
import sys
import time

host = sys.argv[1]
port = int(sys.argv[2])

for _ in range(4):
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(0.25)
    try:
        sock.connect((host, port))
        sock.close()
        sys.exit(0)
    except OSError:
        sock.close()
    time.sleep(0.05)
sys.exit(1)
PY
        return
    fi

    pod_netns_exec sh -c "command -v nc >/dev/null 2>&1 && nc -w 2 $TRAFFIC_TARGET_IP $XDP_TCP_PORT </dev/null"
}

udp_probe_from_pod() {
    local dst_port="${1:-$XDP_UDP_PORT}"
    local src_port="${2:-0}"
    if [ -n "$POD_PID" ] && command -v nsenter >/dev/null 2>&1 &&
        command -v python3 >/dev/null 2>&1; then
        nsenter -t "$POD_PID" -n python3 - \
            "$TRAFFIC_TARGET_IP" "$dst_port" "$src_port" "$POD_IP" <<'PY'
import socket
import sys
import time

host = sys.argv[1]
port = int(sys.argv[2])
src_port = int(sys.argv[3])
pod_ip = sys.argv[4]
payload = b"eulerpilot-real-pod-xdp-udp" * 4

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
if src_port != 0:
    sock.bind((pod_ip, src_port))
for _ in range(8):
    sock.sendto(payload, (host, port))
    time.sleep(0.02)
sock.close()
PY
        return
    fi

    if [ "$src_port" != "0" ]; then
        return 42
    fi

    pod_netns_exec sh -c "command -v nc >/dev/null 2>&1 || exit 42; i=0; while [ \"\$i\" -lt 8 ]; do printf eulerpilot-real-pod-xdp-udp | nc -u -w 1 $TRAFFIC_TARGET_IP $dst_port >/dev/null 2>&1 || true; i=\$((i + 1)); done"
}

extract_xdp_counts() {
    python3 - "$RESULT_DIR/network_policy.jsonl" "$RESULT_DIR/xdp_rule_stats.txt" <<'PY'
import json
import sys

events_path = sys.argv[1]
stats_path = sys.argv[2]
evidence = {}
for line in open(events_path, encoding="utf-8"):
    try:
        item = json.loads(line)
    except json.JSONDecodeError:
        continue
    if item.get("skill") == "network_xdp" and item.get("operation") == "rollback":
        evidence = item.get("evidence", {})

def as_int(key):
    try:
        return int(evidence.get(key, "0"))
    except (TypeError, ValueError):
        return 0

rows = [
    ("total_drop_count", as_int("drop_count")),
    ("drop_icmp_real_pod", as_int("rule.drop_icmp_real_pod.drop_count")),
    ("drop_tcp_real_pod", as_int("rule.drop_tcp_real_pod.drop_count")),
    ("drop_udp_real_pod", as_int("rule.drop_udp_real_pod.drop_count")),
    ("drop_udp_tuple_real_pod", as_int("rule.drop_udp_tuple_real_pod.drop_count")),
    ("drop_udp_tuple_real_pod.src_ip", evidence.get("rule.drop_udp_tuple_real_pod.src_ip", "")),
    ("drop_udp_tuple_real_pod.dst_ip", evidence.get("rule.drop_udp_tuple_real_pod.dst_ip", "")),
    ("drop_udp_tuple_real_pod.src_port", evidence.get("rule.drop_udp_tuple_real_pod.src_port", "")),
    ("drop_udp_tuple_real_pod.dst_port", evidence.get("rule.drop_udp_tuple_real_pod.dst_port", "")),
    ("rule_stats", evidence.get("rule_stats", "")),
]
with open(stats_path, "w", encoding="utf-8") as out:
    for key, value in rows:
        out.write(f"{key}={value}\n")
print(rows[0][1])
PY
}

[ "$(id -u)" -eq 0 ] || fail 'real Pod veth XDP test must run as root'

if ! command -v "$KUBECTL_BIN" >/dev/null 2>&1; then
    write_blocked "missing-kubectl" "install-kubectl-and-provide-eulerpilot-lab-demo-pod"
    write_report
    info "result directory: $RESULT_DIR"
    exit 0
fi
if ! command -v ip >/dev/null 2>&1; then
    write_blocked "iproute2-missing" "install-iproute2"
    write_report
    info "result directory: $RESULT_DIR"
    exit 0
fi
if ! command -v g++ >/dev/null 2>&1; then
    write_blocked "missing-g++" "install-gcc-c++"
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
if ! resolve_host_veth; then
    write_report
    info "result directory: $RESULT_DIR"
    exit 0
fi

if [ ! -x ./build/eulerpilot-agent ] || [ ! -f ./build/network_xdp_demo.bpf.o ]; then
    log_cmd make agent network-xdp-demo
fi
if [ ! -x ./build/eulerpilot-agent ] || [ ! -f ./build/network_xdp_demo.bpf.o ]; then
    write_blocked "network-xdp-build-missing" "run-make-agent-network-xdp-demo"
    write_report
    info "result directory: $RESULT_DIR"
    exit 0
fi

ip link set dev "$HOST_VETH" xdpgeneric off >/dev/null 2>&1 || true
ip link set dev "$HOST_VETH" xdp off >/dev/null 2>&1 || true
ip -d link show "$HOST_VETH" > "$RESULT_DIR/xdp_link_before.txt" 2>&1 || true

if ! ping_host_from_pod > "$RESULT_DIR/baseline_ping.txt" 2>&1; then
    write_blocked "pod-to-host-baseline-ping-failed" "check-pod-network-and-host-ip-connectivity"
    write_report
    info "result directory: $RESULT_DIR"
    exit 0
fi

: > reports/events/network_policy.jsonl
: > run/eulerpilot/action_journal.jsonl

write_agent_config audit audit
timeout "${AGENT_TIMEOUT_S}s" ./build/eulerpilot-agent \
    --config "$RESULT_DIR/agent.audit.yaml" \
    --duration-s "$AGENT_AUDIT_DURATION_S" \
    --interval-ms 1000 \
    --jsonl \
    > "$RESULT_DIR/audit_agent.log" 2>&1
ip -d link show "$HOST_VETH" > "$RESULT_DIR/xdp_link_audit.txt" 2>&1 || true
if has_xdp_attached; then
    fail "audit mode attached XDP unexpectedly on Pod host veth"
fi

write_agent_config enforce enforce
timeout "${AGENT_TIMEOUT_S}s" ./build/eulerpilot-agent \
    --config "$RESULT_DIR/agent.enforce.yaml" \
    --duration-s "$AGENT_ENFORCE_DURATION_S" \
    --interval-ms 500 \
    --jsonl \
    > "$RESULT_DIR/enforce_agent.log" 2>&1 &
AGENT_PID="$!"

if ! wait_for_xdp_attach; then
    ip -d link show "$HOST_VETH" > "$RESULT_DIR/xdp_link_debug.txt" 2>&1 || true
    fail "network_xdp did not attach generic XDP on Pod host veth"
fi
ip -d link show "$HOST_VETH" > "$RESULT_DIR/xdp_link_enforce.txt" 2>&1 || true

if ping_host_from_pod > "$RESULT_DIR/enforce_ping_drop.txt" 2>&1; then
    fail "ICMP ping unexpectedly passed while real Pod host-veth XDP drop is active"
fi

if tcp_probe_from_pod > "$RESULT_DIR/enforce_tcp_drop.txt" 2>&1; then
    fail "TCP probe unexpectedly passed while real Pod host-veth XDP drop is active"
else
    printf 'tcp_probe_result=blocked-or-nc-unavailable\n' >> "$RESULT_DIR/enforce_tcp_drop.txt"
fi

if udp_probe_from_pod "$XDP_UDP_PORT" 0 > "$RESULT_DIR/enforce_udp_drop.txt" 2>&1; then
    printf 'udp_probe_result=sent\n' >> "$RESULT_DIR/enforce_udp_drop.txt"
else
    printf 'udp_probe_result=sent-fallback-unavailable\n' >> "$RESULT_DIR/enforce_udp_drop.txt"
fi

if udp_probe_from_pod "$XDP_UDP_TUPLE_PORT" "$XDP_UDP_TUPLE_SRC_PORT" \
    > "$RESULT_DIR/enforce_udp_tuple_drop.txt" 2>&1; then
    printf 'udp_tuple_probe_result=sent\n' >> "$RESULT_DIR/enforce_udp_tuple_drop.txt"
else
    printf 'udp_tuple_probe_result=sent-fallback-unavailable\n' >> "$RESULT_DIR/enforce_udp_tuple_drop.txt"
fi

wait "$AGENT_PID"
AGENT_PID=""

ip -d link show "$HOST_VETH" > "$RESULT_DIR/xdp_link_rollback.txt" 2>&1 || true
if has_xdp_attached; then
    fail "rollback left XDP attached on Pod host veth"
fi

if ! ping_host_from_pod > "$RESULT_DIR/rollback_ping.txt" 2>&1; then
    fail "Pod-to-host connectivity did not recover after XDP rollback"
fi

cp reports/events/network_policy.jsonl "$RESULT_DIR/network_policy.jsonl"
cp run/eulerpilot/action_journal.jsonl "$RESULT_DIR/action_journal.jsonl"

grep -q '"skill":"network_xdp"' "$RESULT_DIR/network_policy.jsonl" || \
    fail "network_xdp audit event missing"
grep -q '"target_ref":"lab_pod"' "$RESULT_DIR/network_policy.jsonl" || \
    fail "network_xdp audit event missing lab_pod target_ref"
grep -q '"operation":"rollback"' "$RESULT_DIR/network_policy.jsonl" || \
    fail "network_xdp rollback event missing"

XDP_DROP_COUNT="$(extract_xdp_counts)"
if [ "${XDP_DROP_COUNT:-0}" -le 0 ]; then
    fail "network_xdp drop_count did not increase for real Pod host-veth traffic"
fi
XDP_TCP_DROP_COUNT="$(awk -F= '$1=="drop_tcp_real_pod"{print $2}' "$RESULT_DIR/xdp_rule_stats.txt" 2>/dev/null || true)"
if [ "${XDP_TCP_DROP_COUNT:-0}" -le 0 ]; then
    fail "network_xdp TCP rule did not record drops for real Pod host-veth traffic"
fi
XDP_UDP_DROP_COUNT="$(awk -F= '$1=="drop_udp_real_pod"{print $2}' "$RESULT_DIR/xdp_rule_stats.txt" 2>/dev/null || true)"
if [ "${XDP_UDP_DROP_COUNT:-0}" -le 0 ]; then
    fail "network_xdp UDP rule did not record drops for real Pod host-veth traffic"
fi
XDP_UDP_TUPLE_DROP_COUNT="$(awk -F= '$1=="drop_udp_tuple_real_pod"{print $2}' "$RESULT_DIR/xdp_rule_stats.txt" 2>/dev/null || true)"
if [ "${XDP_UDP_TUPLE_DROP_COUNT:-0}" -le 0 ]; then
    fail "network_xdp UDP tuple rule did not record drops for real Pod host-veth traffic"
fi
XDP_UDP_TUPLE_SRC_IP="$(awk -F= '$1=="drop_udp_tuple_real_pod.src_ip"{print $2}' "$RESULT_DIR/xdp_rule_stats.txt" 2>/dev/null || true)"
if [ "$XDP_UDP_TUPLE_SRC_IP" != "$POD_IP" ]; then
    fail "network_xdp UDP tuple source IP evidence mismatch"
fi
XDP_UDP_TUPLE_DST_IP="$(awk -F= '$1=="drop_udp_tuple_real_pod.dst_ip"{print $2}' "$RESULT_DIR/xdp_rule_stats.txt" 2>/dev/null || true)"
if [ "$XDP_UDP_TUPLE_DST_IP" != "$TRAFFIC_TARGET_IP" ]; then
    fail "network_xdp UDP tuple destination IP evidence mismatch"
fi
XDP_UDP_TUPLE_SRC_PORT_EVIDENCE="$(awk -F= '$1=="drop_udp_tuple_real_pod.src_port"{print $2}' "$RESULT_DIR/xdp_rule_stats.txt" 2>/dev/null || true)"
if [ "$XDP_UDP_TUPLE_SRC_PORT_EVIDENCE" != "$XDP_UDP_TUPLE_SRC_PORT" ]; then
    fail "network_xdp UDP tuple source port evidence mismatch"
fi
XDP_UDP_TUPLE_DST_PORT_EVIDENCE="$(awk -F= '$1=="drop_udp_tuple_real_pod.dst_port"{print $2}' "$RESULT_DIR/xdp_rule_stats.txt" 2>/dev/null || true)"
if [ "$XDP_UDP_TUPLE_DST_PORT_EVIDENCE" != "$XDP_UDP_TUPLE_PORT" ]; then
    fail "network_xdp UDP tuple destination port evidence mismatch"
fi

{
    printf 'result=pass\n'
    printf 'reason=real-pod-veth-xdp-attached-dropped-and-restored\n'
    printf 'host=%s\n' "$(hostname 2>/dev/null || printf unknown)"
    printf 'date=%s\n' "$(date -Is)"
    printf 'kernel=%s\n' "$(uname -r)"
    printf 'pod_namespace=%s\n' "$POD_NAMESPACE"
    printf 'pod_name=%s\n' "$POD_NAME"
    printf 'pod_ip=%s\n' "$POD_IP"
    printf 'host_ip=%s\n' "$HOST_IP"
    printf 'traffic_target_ip=%s\n' "$TRAFFIC_TARGET_IP"
    printf 'host_bridge=%s\n' "$HOST_BRIDGE"
    printf 'target_ref=lab_pod\n'
    printf 'host_veth=%s\n' "$HOST_VETH"
    awk -F= '/^(pod_uid|container_id|pid|ifindex|netns_path|cgroup_path)=/{print}' "$RESULT_DIR/resolve_pod_veth.txt"
    printf 'xdp_mode=generic\n'
    printf 'xdp_drop_count=%s\n' "$XDP_DROP_COUNT"
    printf 'xdp_tcp_port=%s\n' "$XDP_TCP_PORT"
    printf 'xdp_tcp_drop_count=%s\n' "$XDP_TCP_DROP_COUNT"
    printf 'xdp_udp_port=%s\n' "$XDP_UDP_PORT"
    printf 'xdp_udp_drop_count=%s\n' "$XDP_UDP_DROP_COUNT"
    printf 'xdp_udp_tuple_src_ip=%s\n' "$POD_IP"
    printf 'xdp_udp_tuple_dst_ip=%s\n' "$TRAFFIC_TARGET_IP"
    printf 'xdp_udp_tuple_src_port=%s\n' "$XDP_UDP_TUPLE_SRC_PORT"
    printf 'xdp_udp_tuple_dst_port=%s\n' "$XDP_UDP_TUPLE_PORT"
    printf 'xdp_udp_tuple_drop_count=%s\n' "$XDP_UDP_TUPLE_DROP_COUNT"
    cat "$RESULT_DIR/xdp_rule_stats.txt"
} > "$RESULT_DIR/summary.txt"

write_report
info "result directory: $RESULT_DIR"
