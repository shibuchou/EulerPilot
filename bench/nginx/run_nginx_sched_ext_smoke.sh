#!/usr/bin/env bash
set -euo pipefail

ROOT="/root/EulerPilot"
OUTDIR="$ROOT/results/reports/nginx-scx-smoke-$(date +%Y%m%d-%H%M%S)"
NGINX_PORT="${NGINX_PORT:-18081}"
INTERVAL_MS="${INTERVAL_MS:-1000}"
STRESS_WORKERS="${STRESS_WORKERS:-2}"
WRK_THREADS="${WRK_THREADS:-2}"
WRK_CONNECTIONS="${WRK_CONNECTIONS:-32}"
WRK_DURATION="${WRK_DURATION:-10s}"
SCX_BIN="${SCX_BIN:-/root/olk/kernel-OLK-6.6-atomgit/tools/sched_ext/build/bin/scx_eulerpilot}"

mkdir -p "$OUTDIR"

cleanup() {
    if [ -n "${STRESS_PID:-}" ]; then
        kill "$STRESS_PID" 2>/dev/null || true
        wait "$STRESS_PID" 2>/dev/null || true
    fi
    if [ -n "${NGINX_PID:-}" ]; then
        kill "$NGINX_PID" 2>/dev/null || true
        wait "$NGINX_PID" 2>/dev/null || true
    fi
    "$ROOT/scripts/rollback.sh" >/dev/null 2>&1 || true
}
trap cleanup EXIT

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

cat > "$OUTDIR/nginx.conf" <<EOF
daemon off;
worker_processes 1;
error_log $OUTDIR/error.log;
pid $OUTDIR/nginx.pid;

events { worker_connections 1024; }

http {
    server {
        listen $NGINX_PORT;
        location / {
            return 200 'EulerPilot nginx sched_ext smoke\n';
        }
    }
}
EOF

printf '[INFO] Nginx sched_ext smoke output: %s\n' "$OUTDIR"

"$ROOT/scripts/rollback.sh" > "$OUTDIR/rollback_before.log" 2>&1 || true
rm -f /tmp/eulerpilot-scx.log

nginx -p "$OUTDIR" -c "$OUTDIR/nginx.conf" &
NGINX_PID=$!
sleep 2

stress-ng --cpu "$STRESS_WORKERS" --timeout 20s > "$OUTDIR/stress_default.log" 2>&1 &
STRESS_PID=$!
sleep 1
run_wrk_with_agent_snapshot default_noisy "" 2 0 cgroup_v2
wait "$STRESS_PID" 2>/dev/null || true
unset STRESS_PID

"$ROOT/scripts/rollback.sh" > "$OUTDIR/rollback_before_sched_ext.log" 2>&1 || true

stress-ng --cpu "$STRESS_WORKERS" --timeout 20s > "$OUTDIR/stress_sched_ext.log" 2>&1 &
STRESS_PID=$!
sleep 1
EULERPILOT_CPU_PSI_THRESHOLD="${EULERPILOT_CPU_PSI_THRESHOLD:-0.0}" \
EULERPILOT_LATENCY_WAIT_THRESHOLD_NS="${EULERPILOT_LATENCY_WAIT_THRESHOLD_NS:-1}" \
EULERPILOT_BACKGROUND_RUNTIME_THRESHOLD_NS="${EULERPILOT_BACKGROUND_RUNTIME_THRESHOLD_NS:-1}" \
EULERPILOT_SCX_BINARY="$SCX_BIN" \
run_wrk_with_agent_snapshot active_noisy_sched_ext --active 4 0 sched_ext
wait "$STRESS_PID" 2>/dev/null || true
unset STRESS_PID

bpftool map dump pinned /sys/fs/bpf/class_map > "$OUTDIR/class_map_dump.json" 2>/dev/null || true
cat /sys/kernel/sched_ext/enable_seq > "$OUTDIR/sched_ext_enable_seq.txt" 2>/dev/null || true
cat /sys/kernel/sched_ext/state > "$OUTDIR/sched_ext_state.txt" 2>/dev/null || true

python3 "$ROOT/scripts/render_nginx_report.py" \
    "$OUTDIR/default_noisy_summary.csv" \
    "$OUTDIR/default_noisy_summary.csv" \
    "$OUTDIR/active_noisy_sched_ext_summary.csv" \
    "$OUTDIR/report.md"

cat > "$OUTDIR/summary.md" <<EOF
# Nginx sched_ext Smoke

- timestamp: $(date --iso-8601=seconds)
- nginx port: $NGINX_PORT
- stress workers: $STRESS_WORKERS
- wrk threads: $WRK_THREADS
- wrk connections: $WRK_CONNECTIONS
- wrk duration: $WRK_DURATION
- scx binary: $SCX_BIN

Generated outputs:

- \`default_noisy_summary.csv\`
- \`active_noisy_sched_ext_summary.csv\`
- \`default_noisy_agent_snapshot.txt\`
- \`active_noisy_sched_ext_agent_snapshot.txt\`
- \`class_map_dump.json\`
- \`sched_ext_enable_seq.txt\`
- \`sched_ext_state.txt\`
EOF

printf '[INFO] Nginx sched_ext smoke complete.\n'
