#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

TOTAL=10
N=1
TMPLOG="/tmp/eulerpilot-sp3-compat-gate.tmp"
MATRIX="${EULERPILOT_SP3_CAPABILITY_MATRIX:-$ROOT_DIR/reports/gates/sp3_capability_matrix.json}"

ok() { echo "ok $N - $1"; N=$((N+1)); }
not_ok() { echo "not ok $N - $1"; cat "$TMPLOG" >&2 2>/dev/null || true; exit 1; }
run_silent() { "$@" >"$TMPLOG" 2>&1; }

echo "1..$TOTAL"

if [[ "${EULERPILOT_COMPAT_ALLOW_NON_SP3:-0}" = "1" ]] ||
   ([[ -r /etc/os-release ]] && grep -Eqi 'openEuler.*24\.03.*SP3|24\.03.*SP3' /etc/os-release); then
    ok "SP3 release marker or explicit compatibility dry-run override"
else
    echo "This gate is for openEuler 24.03 LTS SP3 compatibility." >"$TMPLOG"
    not_ok "SP3 release marker"
fi

if run_silent scripts/generate_capability_matrix.sh "$MATRIX"; then ok "capability matrix generated"; else not_ok "capability matrix"; fi
if run_silent make agent observer; then ok "agent and observer build"; else not_ok "agent/observer build"; fi
if run_silent make network-policy network-qos-tc network-xdp security-policy; then ok "network/security objects compile"; else not_ok "network/security compile"; fi
if run_silent ./build/eulerpilot-agent --validate-config configs/agent.yaml; then ok "default config validates"; else not_ok "default config"; fi
if run_silent ./build/eulerpilot-agent --doctor-safe --config configs/agent.yaml; then ok "safe doctor no side effects"; else not_ok "safe doctor"; fi
if run_silent tests/integration/test_config_validation.sh; then ok "config schema semantic checks"; else not_ok "config schema semantic checks"; fi
if timeout 25s ./build/eulerpilot-agent --config configs/agent.yaml --duration-s 5 --interval-ms 1000 >"$TMPLOG" 2>&1; then ok "cgroup main loop smoke"; else not_ok "cgroup main loop smoke"; fi
if [[ -d /sys/kernel/sched_ext ]]; then
    ok "sched_ext available; SP3 fallback check not required"
else
    if ./build/eulerpilot-agent --status --json >"$TMPLOG" 2>&1; then ok "sched_ext unavailable graceful status"; else not_ok "sched_ext unavailable status"; fi
fi
if ! ip link show ep-veth-qos0 >/dev/null 2>&1 && ! ip link show ep-veth-xdp0 >/dev/null 2>&1; then ok "no lab netdev residue"; else echo "lab netdev residue" >"$TMPLOG"; not_ok "residue check"; fi

echo "sp3_compatibility_gate=complete"
