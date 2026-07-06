#!/usr/bin/env bash
set -euo pipefail

ROOT="/root/EulerPilot"
STAMP="$(date +%Y%m%d-%H%M%S)"
OUTDIR="$ROOT/results/final/nginx-scx-compare-$STAMP"
NGINX_PORT="${NGINX_PORT:-18082}"
INTERVAL_MS="${INTERVAL_MS:-1000}"
STRESS_WORKERS="${STRESS_WORKERS:-2}"
WRK_THREADS="${WRK_THREADS:-2}"
WRK_CONNECTIONS="${WRK_CONNECTIONS:-32}"
WRK_DURATION="${WRK_DURATION:-10s}"
RUNS="${RUNS:-1}"
SCX_BIN="${SCX_BIN:-/root/olk/kernel-OLK-6.6-atomgit/tools/sched_ext/build/bin/scx_eulerpilot}"
SNAPSHOT_DELAY="${SNAPSHOT_DELAY:-0.2}"

LABELS=(
    "quiet_default"
    "quiet_scx_normal"
    "noisy_default"
    "noisy_cgroup_v2"
    "noisy_scx_normal"
    "noisy_scx_always_active"
    "noisy_scx_psi"
)

declare -A LABEL_TITLES=(
    ["quiet_default"]="仅 Nginx，默认调度器"
    ["quiet_scx_normal"]="仅 Nginx，sched_ext 常驻但保持 normal"
    ["noisy_default"]="Nginx + stress-ng，默认调度器"
    ["noisy_cgroup_v2"]="Nginx + stress-ng，cgroup v2 控制"
    ["noisy_scx_normal"]="Nginx + stress-ng，sched_ext normal"
    ["noisy_scx_always_active"]="Nginx + stress-ng，sched_ext always-active"
    ["noisy_scx_psi"]="Nginx + stress-ng，sched_ext psi"
)

RUN_ORDERS=(
    "quiet_default quiet_scx_normal noisy_default noisy_cgroup_v2 noisy_scx_normal noisy_scx_always_active noisy_scx_psi"
    "noisy_scx_psi quiet_default quiet_scx_normal noisy_default noisy_cgroup_v2 noisy_scx_normal noisy_scx_always_active"
    "noisy_scx_always_active noisy_scx_psi quiet_default quiet_scx_normal noisy_default noisy_cgroup_v2 noisy_scx_normal"
)

mkdir -p "$OUTDIR"

cleanup() {
    [ -n "${STRESS_PID:-}" ] && kill "$STRESS_PID" 2>/dev/null || true
    [ -n "${NGINX_PID:-}" ] && kill "$NGINX_PID" 2>/dev/null || true
    "$ROOT/scripts/rollback.sh" >/dev/null 2>&1 || true
}
trap cleanup EXIT

run_wrk_with_snapshot() {
    local rundir="$1"
    local label="$2"
    local active_flag="${3:-}"
    local duration_s="${4:-2}"
    local warmup_cycles="${5:-0}"
    local backend="${6:-cgroup_v2}"

    wrk -t"$WRK_THREADS" -c"$WRK_CONNECTIONS" -d"$WRK_DURATION" --latency "http://127.0.0.1:$NGINX_PORT/" \
        > "$rundir/${label}_wrk.txt" &
    local wrk_pid=$!

    sleep "$SNAPSHOT_DELAY"
    "$ROOT/scripts/capture_agent_snapshot.sh" "$rundir/${label}_agent_snapshot.txt" "$INTERVAL_MS" "$active_flag" "$duration_s" "$warmup_cycles" "$backend"
    wait "$wrk_pid"

    python3 "$ROOT/scripts/extract_wrk_metrics.py" "$rundir/${label}_wrk.txt" "$rundir/${label}_summary.csv"
}

write_group_snapshot() {
    local rundir="$1"
    local label="$2"
    cat /sys/kernel/sched_ext/state > "$rundir/${label}_sched_ext_state.txt" 2>/dev/null || true
    cat /sys/kernel/sched_ext/enable_seq > "$rundir/${label}_sched_ext_enable_seq.txt" 2>/dev/null || true
    cat /sys/kernel/sched_ext/nr_rejected > "$rundir/${label}_sched_ext_nr_rejected.txt" 2>/dev/null || true
    "$SCX_BIN" --gate-status > "$rundir/${label}_gate_status.txt" 2>&1 || true
    python3 "$ROOT/scripts/collect_scx_stats.py" "$rundir/${label}_scx_stats.json" 2>/dev/null || true
    cp /tmp/eulerpilot-psi-gate-trace.jsonl "$rundir/${label}_psi_gate_trace.jsonl" 2>/dev/null || true
}

validate_case_output() {
    local rundir="$1"
    local label="$2"
    local snapshot="$rundir/${label}_agent_snapshot.txt"

    case "$label" in
        noisy_cgroup_v2)
            grep -Eq 'assigned' "$snapshot"
            ;;
        quiet_scx_normal|noisy_scx_normal|noisy_scx_always_active|noisy_scx_psi)
            grep -Eq 'preferred_backend: sched_ext|executor=sched_ext|backend:[[:space:]]+sched_ext' "$snapshot"
            ;;
    esac

    if [ "$label" = "noisy_scx_psi" ]; then
        grep -q '"next_state":"ACTIVE"' "$rundir/${label}_psi_gate_trace.jsonl" 2>/dev/null || \
            grep -Eq 'scx-class-map-updated|gate-state-map-updated' "$snapshot"
    fi
}

mark_invalid_run() {
    local rundir="$1"
    local label="$2"
    local reason="$3"
    printf '%s\n' "$reason" > "$rundir/${label}_invalid_reason.txt"
}

assert_sched_ext_state() {
    local expected="$1"
    local state
    state="$(cat /sys/kernel/sched_ext/state 2>/dev/null || echo unknown)"
    [ "$state" = "$expected" ]
}

run_order_for_index() {
    local run_index="$1"
    local idx=$(( (run_index - 1) % ${#RUN_ORDERS[@]} ))
    printf '%s\n' "${RUN_ORDERS[$idx]}"
}

run_case() {
    local rundir="$1"
    local label="$2"

    rm -f /tmp/eulerpilot-psi-gate-trace.jsonl /tmp/eulerpilot-scx-session.log /tmp/eulerpilot-scx.log
    "$ROOT/scripts/rollback.sh" > "$rundir/${label}_rollback_before.log" 2>&1 || true
    sleep 1

    case "$label" in
        quiet_default)
            assert_sched_ext_state "disabled"
            run_wrk_with_snapshot "$rundir" "$label" "" 2 0 cgroup_v2
            ;;
        quiet_scx_normal)
            assert_sched_ext_state "disabled"
            EULERPILOT_GATE_MODE=normal EULERPILOT_SCX_BINARY="$SCX_BIN" \
                run_wrk_with_snapshot "$rundir" "$label" --active 4 0 sched_ext
            assert_sched_ext_state "disabled"
            ;;
        noisy_default)
            stress-ng --cpu "$STRESS_WORKERS" --timeout 20s > "$rundir/${label}_stress.log" 2>&1 &
            STRESS_PID=$!
            sleep 1
            assert_sched_ext_state "disabled"
            run_wrk_with_snapshot "$rundir" "$label" "" 2 0 cgroup_v2
            wait "$STRESS_PID" 2>/dev/null || true
            unset STRESS_PID
            ;;
        noisy_cgroup_v2)
            "$ROOT/scripts/setup_cgroup_v2.sh" > "$rundir/${label}_setup.log" 2>&1 || true
            stress-ng --cpu "$STRESS_WORKERS" --timeout 20s > "$rundir/${label}_stress.log" 2>&1 &
            STRESS_PID=$!
            sleep 1
            assert_sched_ext_state "disabled"
            run_wrk_with_snapshot "$rundir" "$label" --active 6 2 cgroup_v2
            wait "$STRESS_PID" 2>/dev/null || true
            unset STRESS_PID
            ;;
        noisy_scx_normal|noisy_scx_always_active|noisy_scx_psi)
            local gate_mode="normal"
            if [ "$label" = "noisy_scx_always_active" ]; then
                gate_mode="always-active"
            elif [ "$label" = "noisy_scx_psi" ]; then
                gate_mode="psi"
            fi
            stress-ng --cpu "$STRESS_WORKERS" --timeout 20s > "$rundir/${label}_stress.log" 2>&1 &
            STRESS_PID=$!
            sleep 1
            assert_sched_ext_state "disabled"
            local warmup_cycles=2
            if [ "$label" = "noisy_scx_psi" ]; then
                warmup_cycles=0
            fi
            EULERPILOT_GATE_MODE="$gate_mode" \
            EULERPILOT_SCX_BINARY="$SCX_BIN" \
            EULERPILOT_CPU_PSI_THRESHOLD="${EULERPILOT_CPU_PSI_THRESHOLD:-0.0}" \
            EULERPILOT_LATENCY_WAIT_THRESHOLD_NS="${EULERPILOT_LATENCY_WAIT_THRESHOLD_NS:-1}" \
            EULERPILOT_BACKGROUND_RUNTIME_THRESHOLD_NS="${EULERPILOT_BACKGROUND_RUNTIME_THRESHOLD_NS:-1}" \
            EULERPILOT_GATE_ACTIVATION_WINDOWS="${EULERPILOT_GATE_ACTIVATION_WINDOWS:-1}" \
                run_wrk_with_snapshot "$rundir" "$label" --active 6 "$warmup_cycles" sched_ext
            assert_sched_ext_state "disabled"
            wait "$STRESS_PID" 2>/dev/null || true
            unset STRESS_PID
            ;;
    esac

    write_group_snapshot "$rundir" "$label"
    if ! validate_case_output "$rundir" "$label"; then
        mark_invalid_run "$rundir" "$label" "validation-failed"
        return 1
    fi
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
            return 200 'EulerPilot nginx sched_ext compare\n';
        }
    }
}
EOF

nginx -p "$OUTDIR" -c "$OUTDIR/nginx.conf" &
NGINX_PID=$!
sleep 2

printf '[INFO] Nginx sched_ext compare output: %s\n' "$OUTDIR"

for run in $(seq 1 "$RUNS"); do
    rundir="$OUTDIR/run-$run"
    mkdir -p "$rundir"
    printf '[INFO] run %s/%s\n' "$run" "$RUNS"
    run_order="$(run_order_for_index "$run")"
    printf '%s\n' "$run_order" > "$rundir/run_order.txt"
    for label in $run_order; do
        run_case "$rundir" "$label"
        sleep 2
    done
done

python3 - <<'PY' "$OUTDIR" "$OUTDIR/run_manifest.json" "$RUNS" "$NGINX_PORT" "$WRK_THREADS" "$WRK_CONNECTIONS" "$WRK_DURATION" "$STRESS_WORKERS" "${LABELS[@]}"
import json, sys, subprocess
from pathlib import Path

result_dir = Path(sys.argv[1])
out = Path(sys.argv[2])
runs = int(sys.argv[3])
nginx_port = sys.argv[4]
wrk_threads = sys.argv[5]
wrk_connections = sys.argv[6]
wrk_duration = sys.argv[7]
stress_workers = sys.argv[8]
labels = sys.argv[9:]

label_titles = {
    "quiet_default": "仅 Nginx，默认调度器",
    "quiet_scx_normal": "仅 Nginx，sched_ext 常驻但保持 normal",
    "noisy_default": "Nginx + stress-ng，默认调度器",
    "noisy_cgroup_v2": "Nginx + stress-ng，cgroup v2 控制",
    "noisy_scx_normal": "Nginx + stress-ng，sched_ext normal",
    "noisy_scx_always_active": "Nginx + stress-ng，sched_ext always-active",
    "noisy_scx_psi": "Nginx + stress-ng，sched_ext psi",
}

manifest = {
    "run_id": out.parent.name,
    "timestamp": subprocess.check_output(["date", "--iso-8601=seconds"], text=True).strip(),
    "host": subprocess.check_output(["hostname"], text=True).strip(),
    "kernel_release": subprocess.check_output(["uname", "-r"], text=True).strip(),
    "runs": runs,
    "nginx_port": nginx_port,
    "wrk_threads": wrk_threads,
    "wrk_connections": wrk_connections,
    "wrk_duration": wrk_duration,
    "stress_workers": stress_workers,
    "labels": labels,
    "label_titles": label_titles,
    "summary_paths": {},
    "run_orders": {},
}
for label in labels:
    manifest["summary_paths"][label] = [
        str(path) for path in sorted(result_dir.glob(f"run-*/{label}_summary.csv"))
    ]
for run_dir in sorted(result_dir.glob("run-*")):
    order_file = run_dir / "run_order.txt"
    if order_file.exists():
        manifest["run_orders"][run_dir.name] = order_file.read_text(encoding="utf-8").strip().split()
out.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
PY

python3 "$ROOT/scripts/nginx_compare_summary.py" "$OUTDIR/run_manifest.json" "$OUTDIR/compare_summary_avg.csv"
python3 "$ROOT/scripts/render_nginx_backend_compare_report.py" \
    "$OUTDIR/compare_summary_avg.csv" \
    "$OUTDIR/run_manifest.json" \
    "$OUTDIR/report.md"

cat > "$OUTDIR/summary.md" <<EOF
# Nginx sched_ext 正式对照

- timestamp: $(date --iso-8601=seconds)
- runs: $RUNS
- nginx port: $NGINX_PORT
- wrk threads: $WRK_THREADS
- wrk connections: $WRK_CONNECTIONS
- wrk duration: $WRK_DURATION
- stress workers: $STRESS_WORKERS

本目录包含：

- \`run_manifest.json\`
- \`compare_summary_avg.csv\`
- \`report.md\`
- \`run-*/<label>_summary.csv\`
- \`run-*/<label>_agent_snapshot.txt\`
- \`run-*/<label>_scx_stats.json\`
- \`run-*/<label>_gate_status.txt\`
- \`run-*/run_order.txt\`
- \`run-*/<label>_invalid_reason.txt\`（仅在该组失效时出现）
EOF

printf '[INFO] Nginx sched_ext compare complete: %s\n' "$OUTDIR"
