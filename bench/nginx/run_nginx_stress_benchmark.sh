#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="${ROOT:-$(cd "$SCRIPT_DIR/../.." && pwd)}"
RESULT_ROOT="$ROOT/results"
STAMP="$(date +%Y%m%d-%H%M%S)"
OUTDIR="$RESULT_ROOT/reports/nginx-$STAMP"
NGINX_PORT="${NGINX_PORT:-}"
NGINX_DIR="$OUTDIR/nginx"
NGINX_CONF="$NGINX_DIR/nginx.conf"
NGINX_LOG="$NGINX_DIR/error.log"
INTERVAL_MS="${INTERVAL_MS:-1000}"
STRESS_WORKERS="${STRESS_WORKERS:-2}"
WRK_THREADS="${WRK_THREADS:-2}"
WRK_CONNECTIONS="${WRK_CONNECTIONS:-32}"
WRK_DURATION="${WRK_DURATION:-10s}"
BACKEND="${BACKEND:-cgroup_v2}"
ACTIVE_PREFIX="${ACTIVE_PREFIX:-active_noisy}"

mkdir -p "$OUTDIR" "$NGINX_DIR"

cleanup() {
    if [ -n "${STRESS_PID:-}" ]; then
        kill "$STRESS_PID" 2>/dev/null || true
        wait "$STRESS_PID" 2>/dev/null || true
    fi
    if [ -n "${NGINX_PID:-}" ]; then
        kill "$NGINX_PID" 2>/dev/null || true
        wait "$NGINX_PID" 2>/dev/null || true
    fi
    if [ -f "$NGINX_DIR/nginx.pid" ]; then
        kill "$(cat "$NGINX_DIR/nginx.pid")" 2>/dev/null || true
    fi
}
trap cleanup EXIT

pick_free_port() {
    python3 - <<'PY'
import socket
s = socket.socket()
s.bind(("127.0.0.1", 0))
print(s.getsockname()[1])
s.close()
PY
}

if [ -z "$NGINX_PORT" ]; then
    NGINX_PORT="$(pick_free_port)"
fi

cat > "$NGINX_CONF" <<EOF
daemon off;
worker_processes  1;
error_log  $NGINX_LOG;
pid        $NGINX_DIR/nginx.pid;

events {
    worker_connections  1024;
}

http {
    server {
        listen $NGINX_PORT;
        location / {
            return 200 'EulerPilot nginx benchmark\n';
        }
    }
}
EOF

run_wrk() {
    local prefix="$1"
    wrk -t"$WRK_THREADS" -c"$WRK_CONNECTIONS" -d"$WRK_DURATION" --latency "http://127.0.0.1:$NGINX_PORT/" \
        > "$OUTDIR/${prefix}_wrk.txt"
    python3 "$ROOT/scripts/extract_wrk_metrics.py" "$OUTDIR/${prefix}_wrk.txt" "$OUTDIR/${prefix}_summary.csv"
}

run_wrk_with_agent_snapshot() {
    local prefix="$1"
    local active_flag="${2:-}"
    local duration_s="${3:-2}"
    local warmup_cycles="${4:-0}"
    local backend="${5:-cgroup_v2}"

    wrk -t"$WRK_THREADS" -c"$WRK_CONNECTIONS" -d"$WRK_DURATION" --latency "http://127.0.0.1:$NGINX_PORT/" \
        > "$OUTDIR/${prefix}_wrk.txt" &
    local wrk_pid=$!

    sleep 1
    "$ROOT/scripts/capture_agent_snapshot.sh" "$OUTDIR/${prefix}_agent_snapshot.txt" "$INTERVAL_MS" "$active_flag" "$duration_s" "$warmup_cycles" "$backend"
    wait "$wrk_pid"

    python3 "$ROOT/scripts/extract_wrk_metrics.py" "$OUTDIR/${prefix}_wrk.txt" "$OUTDIR/${prefix}_summary.csv"
}

printf '[INFO] nginx benchmark output: %s\n' "$OUTDIR"
printf '[INFO] backend: %s\n' "$BACKEND"
"$ROOT/scripts/collect_system_snapshot.sh" "$OUTDIR/pre"

nginx -p "$NGINX_DIR" -c "$NGINX_CONF" &
NGINX_PID=$!
sleep 2

run_wrk_with_agent_snapshot baseline "" 2 0 cgroup_v2

stress-ng --cpu "$STRESS_WORKERS" --timeout 20s > "$OUTDIR/stress_ng_default.log" 2>&1 &
STRESS_PID=$!
sleep 1
run_wrk_with_agent_snapshot default_noisy "" 2 0 cgroup_v2
wait "$STRESS_PID" 2>/dev/null || true
unset STRESS_PID

"$ROOT/scripts/rollback.sh" > "$OUTDIR/rollback_before_active.log" 2>&1 || true
if [ "$BACKEND" = "cgroup_v2" ]; then
    "$ROOT/scripts/setup_cgroup_v2.sh" > "$OUTDIR/setup_before_active.log" 2>&1 || true
fi

stress-ng --cpu "$STRESS_WORKERS" --timeout 20s > "$OUTDIR/stress_ng_active.log" 2>&1 &
STRESS_PID=$!
sleep 1
run_wrk_with_agent_snapshot "$ACTIVE_PREFIX" --active 6 2 "$BACKEND"
wait "$STRESS_PID" 2>/dev/null || true
unset STRESS_PID

"$ROOT/scripts/collect_system_snapshot.sh" "$OUTDIR/post"
python3 "$ROOT/scripts/render_nginx_report.py" \
    "$OUTDIR/baseline_summary.csv" \
    "$OUTDIR/default_noisy_summary.csv" \
    "$OUTDIR/${ACTIVE_PREFIX}_summary.csv" \
    "$OUTDIR/report.md"

cat > "$OUTDIR/summary.md" <<EOF
# Nginx + stress-ng Benchmark

- timestamp: $(date --iso-8601=seconds)
- nginx port: $NGINX_PORT
- wrk threads: $WRK_THREADS
- wrk connections: $WRK_CONNECTIONS
- wrk duration: $WRK_DURATION
- stress workers: $STRESS_WORKERS
- backend: $BACKEND
EOF

printf '[INFO] nginx benchmark complete.\n'
