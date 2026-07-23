#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

TOTAL=8
N=1
TMPLOG="/tmp/eulerpilot-sp4-host-gate.tmp"
MATRIX="${EULERPILOT_SP4_CAPABILITY_MATRIX:-$ROOT_DIR/reports/gates/sp4_capability_matrix.json}"

ok() { echo "ok $N - $1"; N=$((N+1)); }
not_ok() { echo "not ok $N - $1"; cat "$TMPLOG" >&2 2>/dev/null || true; exit 1; }
run_silent() { "$@" >"$TMPLOG" 2>&1; }

echo "1..$TOTAL"

if [[ -r /etc/os-release ]] && grep -Eqi 'openEuler.*24\.03.*SP4|24\.03.*SP4' /etc/os-release; then ok "SP4 release marker"; else echo "SP4 marker missing; this host gate should run on 123" >"$TMPLOG"; not_ok "SP4 release marker"; fi
if run_silent scripts/generate_capability_matrix.sh "$MATRIX"; then ok "capability matrix generated"; else not_ok "capability matrix"; fi
if [[ -d /sys/kernel/sched_ext ]]; then ok "sched_ext sysfs available"; else echo "/sys/kernel/sched_ext missing" >"$TMPLOG"; not_ok "sched_ext sysfs"; fi
if command -v scx_eulerpilot >/dev/null 2>&1 || [[ -x /usr/local/bin/scx_eulerpilot ]]; then ok "scx_eulerpilot binary discoverable"; else echo "scx_eulerpilot not found" >"$TMPLOG"; not_ok "scx binary"; fi
if run_silent make agent observer network-policy network-qos-tc network-xdp security-policy; then ok "agent observer and BPF objects build"; else not_ok "build"; fi
if run_silent ./build/eulerpilot-agent --doctor-safe --config configs/agent.yaml; then ok "safe doctor"; else not_ok "safe doctor"; fi
if EULERPILOT_GATE_SMOKE_ROUNDS="${EULERPILOT_GATE_SMOKE_ROUNDS:-1}" EULERPILOT_GATE_DOCTOR_ROUNDS="${EULERPILOT_GATE_DOCTOR_ROUNDS:-1}" scripts/final_quality_gate.sh >"$TMPLOG" 2>&1; then ok "final quality gate reduced/full by env"; else not_ok "final quality gate"; fi
if ! ip link show ep-veth-qos0 >/dev/null 2>&1 && ! ip link show ep-veth-xdp0 >/dev/null 2>&1; then ok "no lab netdev residue"; else echo "lab netdev residue" >"$TMPLOG"; not_ok "residue check"; fi

echo "sp4_host_gate=complete"
