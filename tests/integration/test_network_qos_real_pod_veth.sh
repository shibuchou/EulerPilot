#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR"

RESULT_DIR="${RESULT_DIR:-results/network_policy/real-pod-veth-qos-$(date +%Y%m%d-%H%M%S)}"
RESULT_ABS_DIR=""
KUBECTL_BIN="${EULERPILOT_KUBECTL_BIN:-kubectl}"
KUBECONFIG_PATH="${EULERPILOT_KUBECONFIG:-${KUBECONFIG:-}}"
POD_NAMESPACE="${EULERPILOT_POD_NAMESPACE:-eulerpilot-lab}"
POD_NAME="${EULERPILOT_POD_NAME:-eulerpilot-rc-pod}"
POD_CONTAINER_NAME="${EULERPILOT_POD_CONTAINER_NAME:-}"
POD_IMAGE="${EULERPILOT_POD_IMAGE:-localhost/eulerpilot-busybox:latest}"
POD_IMAGE_PULL_POLICY="${EULERPILOT_POD_IMAGE_PULL_POLICY:-IfNotPresent}"
ALLOW_K8S_CREATE="${EULERPILOT_ALLOW_K8S_CREATE:-0}"
QOS_RATE="${EULERPILOT_POD_QOS_RATE:-1mbit}"
QOS_BURST="${EULERPILOT_POD_QOS_BURST:-32kb}"
QOS_LATENCY="${EULERPILOT_POD_QOS_LATENCY:-50ms}"
AGENT_DURATION_S="${EULERPILOT_REAL_POD_QOS_AGENT_DURATION_S:-8}"
AGENT_TIMEOUT_S="${EULERPILOT_REAL_POD_QOS_AGENT_TIMEOUT_S:-25}"

AGENT_PID=""
POD_CREATED=0
NS_CREATED=0
HOST_VETH=""
POD_IP=""

mkdir -p "$RESULT_DIR" reports/events run/eulerpilot
: > "$RESULT_DIR/commands.log"
: > "$RESULT_DIR/summary.txt"

info() { printf '[INFO] %s\n' "$*"; }
fail() { printf '[FAIL] %s\n' "$*" >&2; exit 1; }

log_cmd() {
    printf '$ %s\n' "$*" >> "$RESULT_DIR/commands.log"
    "$@" >> "$RESULT_DIR/commands.log" 2>&1
}

command_path() {
    command -v "$1" 2>/dev/null || true
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
    local result reason
    result="$(awk -F= '$1=="result"{print $2}' "$RESULT_DIR/summary.txt" 2>/dev/null || true)"
    reason="$(awk -F= '$1=="reason"{print $2}' "$RESULT_DIR/summary.txt" 2>/dev/null || true)"
    cat > "$RESULT_DIR/report.md" <<EOF_REPORT
# Network QoS Real Pod veth Target

- result: \`${result:-unknown}\`
- reason: \`${reason:-unknown}\`
- host: \`$(hostname 2>/dev/null || printf unknown)\`
- kernel: \`$(uname -r)\`
- namespace: \`${POD_NAMESPACE}\`
- pod: \`${POD_NAME}\`
- rate: \`${QOS_RATE}\`

## Purpose

This test validates the real Kubernetes Pod host-veth path for \`network_qos.target_ref\`. It resolves \`type: k8s_pod\` in the lab namespace, attaches TC/TBF to the resolved host veth, sends local host-to-Pod traffic, verifies packet-hit evidence, and checks rollback removes qdisc state.

The script only allows the \`eulerpilot-lab\` namespace by default. It creates the namespace or Pod only when \`EULERPILOT_ALLOW_K8S_CREATE=1\` is set.

## Artifacts

- \`summary.txt\`
- \`commands.log\`
- \`agent.yaml\`
- \`skills.yaml\`
- \`resolve_pod_veth.txt\`
- \`tc_qdisc_before.txt\`, \`tc_qdisc_after.txt\`, \`tc_qdisc_rollback.txt\`
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
        tc qdisc del dev "$HOST_VETH" root >/dev/null 2>&1 || true
        tc qdisc del dev "$HOST_VETH" clsact >/dev/null 2>&1 || true
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

wait_for_qdisc() {
    local deadline=$((SECONDS + 20))
    while [ "$SECONDS" -lt "$deadline" ]; do
        if tc qdisc show dev "$HOST_VETH" 2>/dev/null | grep -q 'tbf' &&
           tc qdisc show dev "$HOST_VETH" 2>/dev/null | grep -q 'clsact'; then
            return 0
        fi
        sleep 0.2
    done
    return 1
}

has_qdisc_residue() {
    tc qdisc show dev "$HOST_VETH" 2>/dev/null | grep -Eq 'tbf|clsact'
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
    if [ -z "$HOST_VETH" ]; then
        write_blocked "pod-veth-ifname-empty" "check-target-resolver-output"
        return 1
    fi
    case "$HOST_VETH" in
        eth*|ens*|eno*|wlan*|bond*|br*|cni*|flannel*)
            write_blocked "pod-veth-denied-host-netdev" "refuse-to-operate-production-or-cni-netdev"
            printf 'host_veth=%s\n' "$HOST_VETH" >> "$RESULT_DIR/summary.txt"
            return 1
            ;;
    esac
}

write_agent_config() {
    local container_name_yaml=""
    if [ -n "$POD_CONTAINER_NAME" ]; then
        container_name_yaml="        container_name: $POD_CONTAINER_NAME"
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
  enabled: true
  config: {}
- name: network_qos
  kind: runtime
  enabled: true
  config:
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
      - name: tc-egress-qos-lab-pod
        hook: tc_egress
        mode: enforce
        target_ref: lab_pod
        protocol: any
        dst_port: '0'
        rate: $QOS_RATE
        burst: $QOS_BURST
        latency: $QOS_LATENCY
        action: limit
YAML
}

[ "$(id -u)" -eq 0 ] || fail 'real Pod veth QoS test must run as root'

if ! command -v "$KUBECTL_BIN" >/dev/null 2>&1; then
    write_blocked "missing-kubectl" "install-kubectl-and-provide-eulerpilot-lab-demo-pod"
    write_report
    info "result directory: $RESULT_DIR"
    exit 0
fi
if ! command -v tc >/dev/null 2>&1 || ! command -v ip >/dev/null 2>&1; then
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

if [ ! -x ./build/eulerpilot-agent ] || [ ! -f ./build/network_qos_tc.bpf.o ]; then
    log_cmd make agent
fi
if [ ! -x ./build/eulerpilot-agent ] || [ ! -f ./build/network_qos_tc.bpf.o ]; then
    write_blocked "network-qos-build-missing" "run-make-agent-and-ensure-network_qos_tc-bpf-object"
    write_report
    info "result directory: $RESULT_DIR"
    exit 0
fi

: > reports/events/network_policy.jsonl
: > run/eulerpilot/action_journal.jsonl
write_agent_config

tc qdisc show dev "$HOST_VETH" > "$RESULT_DIR/tc_qdisc_before.txt" 2>&1 || true

timeout "${AGENT_TIMEOUT_S}s" ./build/eulerpilot-agent \
    --config "$RESULT_DIR/agent.yaml" \
    --duration-s "$AGENT_DURATION_S" \
    --interval-ms 500 \
    --jsonl \
    > "$RESULT_DIR/agent.log" 2>&1 &
AGENT_PID="$!"

if ! wait_for_qdisc; then
    tc qdisc show dev "$HOST_VETH" > "$RESULT_DIR/tc_qdisc_debug.txt" 2>&1 || true
    tc filter show dev "$HOST_VETH" egress > "$RESULT_DIR/tc_filter_debug.txt" 2>&1 || true
    fail "network_qos did not install TC clsact + TBF on Pod host veth"
fi

tc qdisc show dev "$HOST_VETH" > "$RESULT_DIR/tc_qdisc_after.txt" 2>&1 || true
tc filter show dev "$HOST_VETH" egress > "$RESULT_DIR/tc_filter_after.txt" 2>&1 || true
ping -c 5 -W 1 "$POD_IP" > "$RESULT_DIR/ping_pod.txt" 2>&1 || true

wait "$AGENT_PID"
AGENT_PID=""

tc qdisc show dev "$HOST_VETH" > "$RESULT_DIR/tc_qdisc_rollback.txt" 2>&1 || true
if has_qdisc_residue; then
    fail "rollback left TC qdisc residue on Pod host veth"
fi

cp reports/events/network_policy.jsonl "$RESULT_DIR/network_policy.jsonl"
cp run/eulerpilot/action_journal.jsonl "$RESULT_DIR/action_journal.jsonl"

grep -q '"skill":"network_qos"' "$RESULT_DIR/network_policy.jsonl" || \
    fail "network_qos audit event missing"
grep -q '"target_ref":"lab_pod"' "$RESULT_DIR/network_policy.jsonl" || \
    fail "network_qos audit event missing lab_pod target_ref"
grep -q '"operation":"rollback"' "$RESULT_DIR/network_policy.jsonl" || \
    fail "network_qos rollback event missing"
python3 - "$RESULT_DIR/network_policy.jsonl" <<'PY'
import json
import sys
last = None
for line in open(sys.argv[1], encoding="utf-8"):
    try:
        item = json.loads(line)
    except json.JSONDecodeError:
        continue
    if item.get("skill") == "network_qos" and item.get("operation") == "rollback":
        last = item
if last is None:
    raise SystemExit("missing rollback event")
packet_count = int(last.get("evidence", {}).get("packet_count", "0"))
if packet_count <= 0:
    raise SystemExit("network_qos packet_count did not increase")
PY

{
    printf 'result=pass\n'
    printf 'reason=real-pod-veth-qos-applied-and-restored\n'
    printf 'host=%s\n' "$(hostname 2>/dev/null || printf unknown)"
    printf 'date=%s\n' "$(date -Is)"
    printf 'kernel=%s\n' "$(uname -r)"
    printf 'pod_namespace=%s\n' "$POD_NAMESPACE"
    printf 'pod_name=%s\n' "$POD_NAME"
    printf 'pod_ip=%s\n' "$POD_IP"
    printf 'target_ref=lab_pod\n'
    printf 'host_veth=%s\n' "$HOST_VETH"
    awk -F= '/^(pod_uid|container_id|pid|ifindex|netns_path|cgroup_path)=/{print}' "$RESULT_DIR/resolve_pod_veth.txt"
    printf 'qos_rate=%s\n' "$QOS_RATE"
    printf 'qos_burst=%s\n' "$QOS_BURST"
    printf 'qos_latency=%s\n' "$QOS_LATENCY"
} > "$RESULT_DIR/summary.txt"

write_report
info "result directory: $RESULT_DIR"