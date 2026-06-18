#!/usr/bin/env bash
set -euo pipefail

ROOT="/root/EulerPilot"
OUTDIR="$ROOT/results/smoke/gate-mode-$(date +%Y%m%d-%H%M%S)"
MODE="${MODE:?MODE is required (normal|always-active|psi)}"
SCX_BIN="${SCX_BIN:-/root/olk/kernel-OLK-6.6-atomgit/tools/sched_ext/build/bin/scx_eulerpilot}"

mkdir -p "$OUTDIR"

cleanup() {
    "$ROOT/scripts/rollback.sh" >/dev/null 2>&1 || true
}
trap cleanup EXIT

"$ROOT/scripts/rollback.sh" > "$OUTDIR/reset.log" 2>&1 || true
rm -f /tmp/eulerpilot-psi-gate-trace.jsonl /tmp/eulerpilot-scx-session.log

BENCHMARK_COMMAND="./build/eulerpilot-agent --backend sched_ext --gate-mode $MODE"
STRESS_COMMAND=""
BACKEND="sched_ext" EULERPILOT_GATE_MODE="$MODE" python3 "$ROOT/scripts/write_run_manifest.py" "$OUTDIR/run_manifest.json" "gate_mode_smoke"

EULERPILOT_GATE_MODE="$MODE" EULERPILOT_SCX_BINARY="$SCX_BIN" \
  "$ROOT/build/eulerpilot-agent" --backend sched_ext --active --duration-s 4 --interval-ms 1000 \
  > "$OUTDIR/agent.log" 2>&1 || true

cat /sys/kernel/sched_ext/state > "$OUTDIR/final_state.txt"
cat /sys/kernel/sched_ext/nr_rejected > "$OUTDIR/nr_rejected.txt"
cp /tmp/eulerpilot-psi-gate-trace.jsonl "$OUTDIR/psi_gate_trace.jsonl" 2>/dev/null || true
"$SCX_BIN" --gate-status > "$OUTDIR/gate_status.txt" 2>&1 || true
"$SCX_BIN" --stats > "$OUTDIR/scx_stats.txt" 2>&1 || true

printf '[INFO] gate mode smoke output: %s\n' "$OUTDIR"
