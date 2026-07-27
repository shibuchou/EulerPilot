#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="${ROOT:-$(cd "$SCRIPT_DIR/../.." && pwd)}"
OUTDIR="$ROOT/results/smoke/loader-wiring-$(date +%Y%m%d-%H%M%S)"
SCX_BIN="${SCX_BIN:-$(command -v scx_eulerpilot 2>/dev/null || true)}"
SCX_BIN="${SCX_BIN:-/usr/local/bin/scx_eulerpilot}"

mkdir -p "$OUTDIR"

cleanup() {
    "$ROOT/scripts/rollback.sh" >/dev/null 2>&1 || true
}
trap cleanup EXIT

"$ROOT/scripts/rollback.sh" > "$OUTDIR/reset.log" 2>&1 || true

BENCHMARK_COMMAND="$SCX_BIN"
STRESS_COMMAND=""
python3 "$ROOT/scripts/write_run_manifest.py" "$OUTDIR/run_manifest.json" "loader_wiring_smoke"

nohup "$SCX_BIN" > "$OUTDIR/loader.log" 2>&1 </dev/null &
LOADER_PID=$!
sleep 2

cat /sys/kernel/sched_ext/state > "$OUTDIR/sched_ext_state_enabled.txt"
"$SCX_BIN" --gate-status > "$OUTDIR/gate_status_initial.txt" 2>&1 || true
"$SCX_BIN" --gate-set active > "$OUTDIR/gate_set_active.txt" 2>&1 || true
"$SCX_BIN" --gate-status > "$OUTDIR/gate_status_active.txt" 2>&1 || true
"$SCX_BIN" --gate-set normal > "$OUTDIR/gate_set_normal.txt" 2>&1 || true
"$SCX_BIN" --gate-status > "$OUTDIR/gate_status_normal.txt" 2>&1 || true
"$SCX_BIN" --stats > "$OUTDIR/scx_stats.txt" 2>&1 || true
"$SCX_BIN" --detach > "$OUTDIR/detach.txt" 2>&1 || true
sleep 1
cat /sys/kernel/sched_ext/state > "$OUTDIR/sched_ext_state_disabled.txt"
cat /sys/kernel/sched_ext/nr_rejected > "$OUTDIR/nr_rejected.txt"

printf '[INFO] loader wiring smoke output: %s\n' "$OUTDIR"
