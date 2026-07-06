#!/usr/bin/env bash
set -euo pipefail

ROOT="/root/EulerPilot"
STAMP="$(date +%Y%m%d-%H%M%S)"
OUTDIR="$ROOT/results/final/redis-scx-psi-probe-$STAMP"
REDIS_PORT="${REDIS_PORT:-6390}"
INTERVAL_MS="${INTERVAL_MS:-500}"
DURATION_S="${DURATION_S:-6}"
STRESS_WORKERS="${STRESS_WORKERS:-4}"
BENCH_CLIENTS="${BENCH_CLIENTS:-64}"
BENCH_REQUESTS="${BENCH_REQUESTS:-30000}"
BENCH_TESTS="${BENCH_TESTS:-set,get,incr}"
SCX_BIN="${SCX_BIN:-/usr/local/bin/scx_eulerpilot}"

mkdir -p "$OUTDIR"

cleanup() {
    [ -n "${BENCH_LOOP_PID:-}" ] && kill "$BENCH_LOOP_PID" 2>/dev/null || true
    [ -n "${STRESS_PID:-}" ] && kill "$STRESS_PID" 2>/dev/null || true
    [ -n "${REDIS_PID:-}" ] && kill "$REDIS_PID" 2>/dev/null || true
    pkill -x redis-benchmark 2>/dev/null || true
    pkill -x stress-ng 2>/dev/null || true
    pkill -x stress-ng-cpu 2>/dev/null || true
    EULERPILOT_SCX_BINARY="$SCX_BIN" "$ROOT/scripts/rollback.sh" >/dev/null 2>&1 || true
}
trap cleanup EXIT

cat > "$OUTDIR/redis.conf" <<EOF
bind 127.0.0.1
port $REDIS_PORT
save ""
appendonly no
daemonize no
protected-mode no
dir $OUTDIR
logfile $OUTDIR/redis.log
EOF

rm -f /tmp/eulerpilot-psi-gate-trace.jsonl /tmp/eulerpilot-scx-session.log
EULERPILOT_SCX_BINARY="$SCX_BIN" "$ROOT/scripts/rollback.sh" > "$OUTDIR/rollback_before.log" 2>&1 || true

redis-server "$OUTDIR/redis.conf" >/dev/null 2>&1 &
REDIS_PID=$!
sleep 1

stress-ng --cpu "$STRESS_WORKERS" --timeout "$((DURATION_S + 12))s" > "$OUTDIR/stress.log" 2>&1 &
STRESS_PID=$!

(
    trap 'kill "${child:-0}" 2>/dev/null || true; exit 0' TERM INT
    while :; do
        redis-benchmark -h 127.0.0.1 -p "$REDIS_PORT" \
            -t "$BENCH_TESTS" \
            -c "$BENCH_CLIENTS" \
            -n "$BENCH_REQUESTS" \
            --csv >> "$OUTDIR/redis_benchmark.csv" \
            2>> "$OUTDIR/redis_benchmark.err" &
        child=$!
        wait "$child" || true
    done
) &
BENCH_LOOP_PID=$!

sleep 0.2
EULERPILOT_GATE_MODE=psi \
EULERPILOT_SCX_BINARY="$SCX_BIN" \
EULERPILOT_CPU_PSI_THRESHOLD="${EULERPILOT_CPU_PSI_THRESHOLD:-0.0}" \
EULERPILOT_LATENCY_WAIT_THRESHOLD_NS="${EULERPILOT_LATENCY_WAIT_THRESHOLD_NS:-1}" \
EULERPILOT_BACKGROUND_RUNTIME_THRESHOLD_NS="${EULERPILOT_BACKGROUND_RUNTIME_THRESHOLD_NS:-1}" \
EULERPILOT_GATE_ACTIVATION_WINDOWS="${EULERPILOT_GATE_ACTIVATION_WINDOWS:-1}" \
    "$ROOT/build/eulerpilot-agent" \
    --config "$ROOT/configs/agent.yaml" \
    --backend sched_ext \
    --interval-ms "$INTERVAL_MS" \
    --duration-s "$DURATION_S" \
    --warmup-cycles 0 \
    --active > "$OUTDIR/agent_snapshot.txt"

cp /tmp/eulerpilot-psi-gate-trace.jsonl "$OUTDIR/psi_gate_trace.jsonl" 2>/dev/null || true
"$SCX_BIN" --gate-status > "$OUTDIR/gate_status.txt" 2>&1 || true
python3 "$ROOT/scripts/collect_scx_stats.py" "$OUTDIR/scx_stats.json" 2>/dev/null || true

if ! grep -q '"next_state":"ACTIVE"' "$OUTDIR/psi_gate_trace.jsonl" 2>/dev/null; then
    printf '[FAIL] PSI gate did not enter ACTIVE. See %s\n' "$OUTDIR" >&2
    exit 1
fi

cat > "$OUTDIR/summary.md" <<EOF
# Redis sched_ext PSI ACTIVE Probe

- timestamp: $(date --iso-8601=seconds)
- redis port: $REDIS_PORT
- stress workers: $STRESS_WORKERS
- bench clients: $BENCH_CLIENTS
- bench requests: $BENCH_REQUESTS
- bench tests: $BENCH_TESTS
- scx binary: $SCX_BIN
- result: PSI gate entered ACTIVE

Evidence files:

- \`agent_snapshot.txt\`
- \`psi_gate_trace.jsonl\`
- \`gate_status.txt\`
- \`scx_stats.json\`
- \`redis_benchmark.csv\`
- \`stress.log\`
EOF

printf '[INFO] Redis sched_ext PSI probe complete: %s\n' "$OUTDIR"
