#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="${ROOT:-$(cd "$SCRIPT_DIR/../.." && pwd)}"
OUTDIR="$ROOT/results/reports/redis-scx-smoke-$(date +%Y%m%d-%H%M%S)"
REDIS_PORT="${REDIS_PORT:-6384}"
INTERVAL_MS="${INTERVAL_MS:-1000}"
STRESS_WORKERS="${STRESS_WORKERS:-2}"
BENCH_CLIENTS="${BENCH_CLIENTS:-16}"
BENCH_REQUESTS="${BENCH_REQUESTS:-20000}"
SCX_BIN="${SCX_BIN:-$(command -v scx_eulerpilot 2>/dev/null || true)}"
SCX_BIN="${SCX_BIN:-/usr/local/bin/scx_eulerpilot}"
LEGACY_SCX_BIN="${LEGACY_SCX_BIN:-/root/olk/kernel-OLK-6.6-atomgit/tools/sched_ext/build/bin/scx_eulerpilot}"
if [ ! -x "$SCX_BIN" ] && [ "${ALLOW_LEGACY_SCX_FALLBACK:-0}" = "1" ] && [ -x "$LEGACY_SCX_BIN" ]; then
    printf '[WARN] using legacy scx fallback: %s\n' "$LEGACY_SCX_BIN" >&2
    SCX_BIN="$LEGACY_SCX_BIN"
fi

mkdir -p "$OUTDIR"

cleanup() {
    if [ -n "${STRESS_PID:-}" ]; then
        kill "$STRESS_PID" 2>/dev/null || true
        wait "$STRESS_PID" 2>/dev/null || true
    fi
    redis-cli -p "$REDIS_PORT" shutdown nosave >/dev/null 2>&1 || true
    "$ROOT/scripts/rollback.sh" >/dev/null 2>&1 || true
}
trap cleanup EXIT

run_benchmark() {
    local prefix="$1"
    redis-benchmark -h 127.0.0.1 -p "$REDIS_PORT" -c "$BENCH_CLIENTS" -n "$BENCH_REQUESTS" \
        --csv > "$OUTDIR/${prefix}_redis_benchmark.csv"
    awk -f "$ROOT/scripts/extract_redis_metrics.awk" \
        "$OUTDIR/${prefix}_redis_benchmark.csv" > "$OUTDIR/${prefix}_summary.csv"
}

printf '[INFO] Redis sched_ext smoke output: %s\n' "$OUTDIR"
printf '[INFO] redis port=%s stress_workers=%s clients=%s requests=%s\n' \
    "$REDIS_PORT" "$STRESS_WORKERS" "$BENCH_CLIENTS" "$BENCH_REQUESTS"

"$ROOT/scripts/rollback.sh" > "$OUTDIR/rollback_before.log" 2>&1 || true
rm -f /tmp/eulerpilot-scx.log

redis-server --port "$REDIS_PORT" --save "" --appendonly no --daemonize yes
sleep 2

stress-ng --cpu "$STRESS_WORKERS" --timeout 20s > "$OUTDIR/stress_default.log" 2>&1 &
STRESS_PID=$!
sleep 1
run_benchmark default_noisy
"$ROOT/scripts/capture_agent_snapshot.sh" "$OUTDIR/default_noisy_agent_snapshot.txt" "$INTERVAL_MS" "" 2 0 cgroup_v2
wait "$STRESS_PID" 2>/dev/null || true
unset STRESS_PID

"$ROOT/scripts/rollback.sh" > "$OUTDIR/rollback_before_sched_ext.log" 2>&1 || true

stress-ng --cpu "$STRESS_WORKERS" --timeout 20s > "$OUTDIR/stress_sched_ext.log" 2>&1 &
STRESS_PID=$!
sleep 1
EULERPILOT_SCX_BINARY="$SCX_BIN" timeout 3s "$SCX_BIN" >/dev/null 2>&1 || true
python3 "$ROOT/scripts/collect_scx_stats.py" "$OUTDIR/scx_stats_before.json"
EULERPILOT_CPU_PSI_THRESHOLD="${EULERPILOT_CPU_PSI_THRESHOLD:-0.0}" \
EULERPILOT_LATENCY_WAIT_THRESHOLD_NS="${EULERPILOT_LATENCY_WAIT_THRESHOLD_NS:-1}" \
EULERPILOT_BACKGROUND_RUNTIME_THRESHOLD_NS="${EULERPILOT_BACKGROUND_RUNTIME_THRESHOLD_NS:-1}" \
EULERPILOT_SCX_BINARY="$SCX_BIN" \
"$ROOT/scripts/capture_agent_snapshot.sh" "$OUTDIR/active_noisy_sched_ext_agent_snapshot.txt" "$INTERVAL_MS" --active 4 0 sched_ext
run_benchmark active_noisy_sched_ext
python3 "$ROOT/scripts/collect_scx_stats.py" "$OUTDIR/scx_stats_after.json"
python3 "$ROOT/scripts/collect_scx_stats.py" "$OUTDIR/scx_stats_delta.json" "$OUTDIR/scx_stats_before.json"
wait "$STRESS_PID" 2>/dev/null || true
unset STRESS_PID

bpftool map dump pinned /sys/fs/bpf/class_map > "$OUTDIR/class_map_dump.json" 2>/dev/null || true
cat /sys/kernel/sched_ext/enable_seq > "$OUTDIR/sched_ext_enable_seq.txt" 2>/dev/null || true
cat /sys/kernel/sched_ext/state > "$OUTDIR/sched_ext_state.txt" 2>/dev/null || true

cat > "$OUTDIR/summary.md" <<EOF
# Redis sched_ext Smoke

- timestamp: $(date --iso-8601=seconds)
- redis port: $REDIS_PORT
- stress workers: $STRESS_WORKERS
- benchmark clients: $BENCH_CLIENTS
- benchmark requests: $BENCH_REQUESTS
- scx binary: $SCX_BIN

Generated outputs:

- \`default_noisy_summary.csv\`
- \`active_noisy_sched_ext_summary.csv\`
- \`default_noisy_agent_snapshot.txt\`
- \`active_noisy_sched_ext_agent_snapshot.txt\`
- \`class_map_dump.json\`
- \`scx_stats_before.json\`
- \`scx_stats_after.json\`
- \`scx_stats_delta.json\`
- \`sched_ext_enable_seq.txt\`
- \`sched_ext_state.txt\`

This smoke run is used to verify the \`class_map -> scx_eulerpilot\` control path before formal sched_ext experiments.
EOF

printf '[INFO] Redis sched_ext smoke complete.\n'
