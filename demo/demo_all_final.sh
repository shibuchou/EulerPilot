#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MODE="live"
LIVE_CLEANUP_ARMED=0

usage() {
    cat <<USAGE
Usage: demo/demo_all_final.sh --mode live|offline|cleanup
       demo/demo_all_final.sh --cleanup
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --mode)
            MODE="${2:-}"
            shift 2
            ;;
        --cleanup)
            MODE="cleanup"
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

section() {
    printf '\n== %s ==\n' "$1"
}

require_root_for_live() {
    if [[ $EUID -ne 0 ]]; then
        echo "live/cleanup mode requires root" >&2
        exit 1
    fi
}

cleanup_mode() {
    require_root_for_live
    section "cleanup lab network"
    ip link del ep-veth-pe0 2>/dev/null || true
    ip netns del ep-pe-ns 2>/dev/null || true
    section "cleanup lab cgroup"
    if [[ -d /sys/fs/cgroup/eulerpilot/policy-engine-v3-resource ]]; then
        rmdir /sys/fs/cgroup/eulerpilot/policy-engine-v3-resource 2>/dev/null || true
    fi
    section "cleanup helper scripts"
    "$ROOT_DIR/scripts/rollback.sh" || true
    "$ROOT_DIR/scripts/cleanup_network_qos_tc.sh" || true
    echo "cleanup=done"
}

live_cleanup_trap() {
    local primary_status=$?
    local cleanup_status=0
    trap - EXIT INT TERM HUP
    if [[ "$LIVE_CLEANUP_ARMED" == "1" ]]; then
        echo "primary_failure=$primary_status"
        cleanup_mode || cleanup_status=$?
        echo "cleanup_failure=$cleanup_status"
    fi
    if [[ "$primary_status" -ne 0 ]]; then
        exit "$primary_status"
    fi
    if [[ "$cleanup_status" -ne 0 ]]; then
        exit "$cleanup_status"
    fi
}

offline_mode() {
    section "evidence index"
    sed -n '1,180p' "$ROOT_DIR/docs/final_evidence_index.md"

    section "latest policy engine v3.1 results"
    find "$ROOT_DIR/results/policy_engine" -maxdepth 1 -type d -name 'security-network-resource-*' 2>/dev/null | sort | tail -5 || true

    latest="$(find "$ROOT_DIR/results/policy_engine" -maxdepth 1 -type d -name 'security-network-resource-*' 2>/dev/null | sort | tail -1 || true)"
    if [[ -n "$latest" ]]; then
        section "summary"
        [[ -r "$latest/summary.txt" ]] && cat "$latest/summary.txt" || true
        section "report"
        [[ -r "$latest/report.md" ]] && sed -n '1,160p' "$latest/report.md" || true
    fi
}

live_mode() {
    require_root_for_live
    cd "$ROOT_DIR"
    LIVE_CLEANUP_ARMED=1
    trap live_cleanup_trap EXIT INT TERM HUP

    section "check_env"
    ./scripts/check_env.sh || true

    section "build agent and demos"
    make agent security-policy network-qos-tc

    section "list-skills"
    ./build/eulerpilot-agent --list-skills

    section "validate default config"
    ./build/eulerpilot-agent --validate-config configs/agent.yaml

    section "validate v3.1 config"
    ./build/eulerpilot-agent --validate-config configs/policy_engine_security_network_resource.yaml

    section "safe doctor default skills"
    ./build/eulerpilot-agent --doctor-safe --config configs/agent.yaml

    section "status json"
    ./build/eulerpilot-agent --status --json

    section "offline evidence preview"
    demo/demo_all_final.sh --mode offline || true

    section "policy engine cross-skill v2 live test"
    tests/integration/test_policy_engine_security_network_resource.sh

    echo "demo_live=pass"
}

case "$MODE" in
    live) live_mode ;;
    offline) offline_mode ;;
    cleanup) cleanup_mode ;;
    *)
        echo "invalid mode: $MODE" >&2
        usage >&2
        exit 2
        ;;
esac
