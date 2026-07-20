#!/usr/bin/env bash
set -euo pipefail

[ $# -ge 2 ] || { printf 'usage: %s <outdir> <prefix>\n' "$(basename "$0")" >&2; exit 1; }

OUTDIR="$1"
PREFIX="$2"
SCX_BIN="${SCX_BIN:-$(command -v scx_eulerpilot 2>/dev/null || true)}"
SCX_BIN="${SCX_BIN:-/usr/local/bin/scx_eulerpilot}"

mkdir -p "$OUTDIR"

count_process_comm() {
    local target="$1"
    ps -eo comm= 2>/dev/null | awk -v target="$target" '$1 == target { count++ } END { print count + 0 }'
}

cat /sys/kernel/sched_ext/state > "$OUTDIR/${PREFIX}_sched_ext_state.txt" 2>/dev/null || true
cat /sys/kernel/sched_ext/enable_seq > "$OUTDIR/${PREFIX}_enable_seq.txt" 2>/dev/null || true
cat /sys/kernel/sched_ext/nr_rejected > "$OUTDIR/${PREFIX}_nr_rejected.txt" 2>/dev/null || true
printf 'gate_relevant_latency_count=%s\n' "$(pgrep -fc 'redis-server .*639' || true)" > "$OUTDIR/${PREFIX}_preflight.txt"
printf 'gate_relevant_background_count=%s\n' "$(( $(count_process_comm "stress-ng-cpu") + $(count_process_comm "stress-ng") ))" >> "$OUTDIR/${PREFIX}_preflight.txt"
"$SCX_BIN" --gate-status > "$OUTDIR/${PREFIX}_gate_status.txt" 2>&1 || true
"$SCX_BIN" --stats > "$OUTDIR/${PREFIX}_scx_stats.txt" 2>&1 || true
cp /tmp/eulerpilot-psi-gate-trace.jsonl "$OUTDIR/${PREFIX}_psi_gate_trace.jsonl" 2>/dev/null || true
cp /tmp/eulerpilot-scx-session.log "$OUTDIR/${PREFIX}_scx_session.log" 2>/dev/null || true
