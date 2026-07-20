#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="${ROOT:-$(cd "$SCRIPT_DIR/../.." && pwd)}"
RESULT_ROOT="$ROOT/results"
STAMP="$(date +%Y%m%d-%H%M%S)"
OUTDIR="$RESULT_ROOT/reports/redis-$STAMP"
REDIS_PORT="${REDIS_PORT:-6380}"
REDIS_DIR="$OUTDIR/redis"
REDIS_CONF="$REDIS_DIR/redis.conf"
REDIS_LOG="$REDIS_DIR/redis.log"
INTERVAL_MS="${INTERVAL_MS:-1000}"
STRESS_WORKERS="${STRESS_WORKERS:-4}"
BENCH_CLIENTS="${BENCH_CLIENTS:-32}"
BENCH_REQUESTS="${BENCH_REQUESTS:-10000}"
RUNS="${RUNS:-3}"
BACKEND="${BACKEND:-cgroup_v2}"
ACTIVE_PREFIX="${ACTIVE_PREFIX:-active_noisy}"

mkdir -p "$OUTDIR" "$REDIS_DIR"

cleanup() {
    if [ -n "${STRESS_PID:-}" ]; then
        kill "$STRESS_PID" 2>/dev/null || true
        wait "$STRESS_PID" 2>/dev/null || true
    fi
    if [ -n "${REDIS_PID:-}" ]; then
        kill "$REDIS_PID" 2>/dev/null || true
        wait "$REDIS_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT

run_benchmark() {
    local rundir="$1"
    local prefix="$2"
    redis-benchmark -h 127.0.0.1 -p "$REDIS_PORT" -c "$BENCH_CLIENTS" -n "$BENCH_REQUESTS" \
        --csv > "$rundir/${prefix}_redis_benchmark.csv"
    awk -f "$ROOT/scripts/extract_redis_metrics.awk" \
        "$rundir/${prefix}_redis_benchmark.csv" > "$rundir/${prefix}_summary.csv"
}

emit_compare_csv() {
    local rundir="$1"
    awk -f "$ROOT/scripts/compare_redis_summaries.awk" \
        "$rundir/baseline_summary.csv" \
        "$rundir/default_noisy_summary.csv" \
        "$rundir/${ACTIVE_PREFIX}_summary.csv" > "$rundir/compare_summary.csv"
}

cat > "$REDIS_CONF" <<EOF
bind 127.0.0.1
port $REDIS_PORT
save ""
appendonly no
daemonize no
protected-mode no
dir $REDIS_DIR
logfile $REDIS_LOG
EOF

printf '[INFO] benchmark output: %s\n' "$OUTDIR"
printf '[INFO] redis port: %s\n' "$REDIS_PORT"
printf '[INFO] runs: %s\n' "$RUNS"
printf '[INFO] backend: %s\n' "$BACKEND"

"$ROOT/scripts/collect_system_snapshot.sh" "$OUTDIR/pre"

redis-server "$REDIS_CONF" > /dev/null 2>&1 &
REDIS_PID=$!
sleep 2

COMPARE_INPUTS=()
for run in $(seq 1 "$RUNS"); do
    RUNDIR="$OUTDIR/run-$run"
    mkdir -p "$RUNDIR"
    printf '[INFO] starting run %s/%s\n' "$run" "$RUNS"
    sleep 3

    run_benchmark "$RUNDIR" baseline
    "$ROOT/scripts/capture_agent_snapshot.sh" "$RUNDIR/baseline_agent_snapshot.txt" "$INTERVAL_MS" "" 2 0

    stress-ng --cpu "$STRESS_WORKERS" --timeout 20s > "$RUNDIR/stress_ng_default.log" 2>&1 &
    STRESS_PID=$!
    sleep 1
    run_benchmark "$RUNDIR" default_noisy
    "$ROOT/scripts/capture_agent_snapshot.sh" "$RUNDIR/default_noisy_agent_snapshot.txt" "$INTERVAL_MS" "" 2 0
    wait "$STRESS_PID" 2>/dev/null || true
    unset STRESS_PID

    "$ROOT/scripts/rollback.sh" > "$RUNDIR/rollback_before_active.log" 2>&1 || true
    if [ "$BACKEND" = "cgroup_v2" ]; then
        "$ROOT/scripts/setup_cgroup_v2.sh" > "$RUNDIR/setup_before_active.log" 2>&1 || true
    fi

    stress-ng --cpu "$STRESS_WORKERS" --timeout 20s > "$RUNDIR/stress_ng_active.log" 2>&1 &
    STRESS_PID=$!
    sleep 1
    "$ROOT/scripts/capture_agent_snapshot.sh" "$RUNDIR/${ACTIVE_PREFIX}_agent_snapshot.txt" "$INTERVAL_MS" --active 6 2 "$BACKEND"
    run_benchmark "$RUNDIR" "$ACTIVE_PREFIX"
    wait "$STRESS_PID" 2>/dev/null || true
    unset STRESS_PID

    emit_compare_csv "$RUNDIR"
    COMPARE_INPUTS+=("$RUNDIR/compare_summary.csv")
done

python3 "$ROOT/scripts/aggregate_compare_csv.py" "$OUTDIR/compare_summary_avg.csv" "${COMPARE_INPUTS[@]}"
python3 "$ROOT/scripts/render_redis_report.py" "$OUTDIR/compare_summary_avg.csv" "$OUTDIR/report.md"
"$ROOT/scripts/collect_system_snapshot.sh" "$OUTDIR/post"

cat > "$OUTDIR/summary.md" <<EOF
# Redis + stress-ng Benchmark

- timestamp: $(date --iso-8601=seconds)
- redis port: $REDIS_PORT
- benchmark clients: $BENCH_CLIENTS
- benchmark requests: $BENCH_REQUESTS
- stress workers: $STRESS_WORKERS
- runs: $RUNS
- backend: $BACKEND

Generated phases per run:

- \`baseline\`
- \`default_noisy\`
- \`$ACTIVE_PREFIX\`

Top-level outputs:

- \`compare_summary_avg.csv\`
- \`report.md\`
- \`run-*/compare_summary.csv\`
- \`run-*/${ACTIVE_PREFIX}_agent_snapshot.txt\`

This run compares the default noisy case and the EulerPilot active noisy case across multiple repetitions using the same Redis and stress-ng workload shape.
EOF

printf '[INFO] redis benchmark complete.\n'
