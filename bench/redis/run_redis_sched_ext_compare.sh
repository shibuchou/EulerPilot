#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="${ROOT:-$(cd "$SCRIPT_DIR/../.." && pwd)}"
STAMP="$(date +%Y%m%d-%H%M%S)"
OUTDIR="${OUTDIR:-$ROOT/results/final/redis-scx-compare-$STAMP}"
REDIS_PORT="${REDIS_PORT:-6386}"
INTERVAL_MS="${INTERVAL_MS:-1000}"
STRESS_WORKERS="${STRESS_WORKERS:-2}"
BENCH_CLIENTS="${BENCH_CLIENTS:-16}"
BENCH_REQUESTS="${BENCH_REQUESTS:-20000}"
RUNS="${RUNS:-3}"
SCX_BIN="${SCX_BIN:-$(command -v scx_eulerpilot 2>/dev/null || true)}"
SCX_BIN="${SCX_BIN:-/usr/local/bin/scx_eulerpilot}"
SNAPSHOT_DELAY="${SNAPSHOT_DELAY:-0.2}"
PSI_PROBE_CLIENTS="${PSI_PROBE_CLIENTS:-64}"
PSI_PROBE_REQUESTS="${PSI_PROBE_REQUESTS:-30000}"
PSI_PROBE_TESTS="${PSI_PROBE_TESTS:-set,get,incr}"
RUN_RANDOM_SEED="${RUN_RANDOM_SEED:-$STAMP}"
BENCH_CGROUP_ROOT="${BENCH_CGROUP_ROOT:-/sys/fs/cgroup/eulerpilot-bench}"
BENCH_RUN_ID="$(basename "$OUTDIR")"
BENCH_RUN_CGROUP_ROOT="$BENCH_CGROUP_ROOT/$BENCH_RUN_ID"
export RUN_RANDOM_SEED BENCH_RUN_CGROUP_ROOT

LABELS=(
    "quiet_default"
    "quiet_scx_normal"
    "noisy_default"
    "noisy_cgroup_v2"
    "noisy_scx_normal"
    "noisy_scx_always_active"
    "noisy_scx_psi"
)

if [ -n "${LABEL_FILTER:-}" ]; then
    IFS=',' read -r -a LABELS <<< "$LABEL_FILTER"
fi

declare -A LABEL_TITLES=(
    ["quiet_default"]="仅 Redis，默认调度器"
    ["quiet_scx_normal"]="仅 Redis，sched_ext 常驻但保持 normal"
    ["noisy_default"]="Redis + stress-ng，默认调度器"
    ["noisy_cgroup_v2"]="Redis + stress-ng，cgroup v2 控制"
    ["noisy_scx_normal"]="Redis + stress-ng，sched_ext normal"
    ["noisy_scx_always_active"]="Redis + stress-ng，sched_ext always-active"
    ["noisy_scx_psi"]="Redis + stress-ng，sched_ext psi"
)

mkdir -p "$OUTDIR"
OUTDIR="$(cd "$OUTDIR" && pwd)"
BENCH_RUN_ID="$(basename "$OUTDIR")"
BENCH_RUN_CGROUP_ROOT="$BENCH_CGROUP_ROOT/$BENCH_RUN_ID"
export RUN_RANDOM_SEED BENCH_RUN_CGROUP_ROOT

cleanup() {
    [ -n "${STRESS_PID:-}" ] && kill "$STRESS_PID" 2>/dev/null || true
    [ -n "${PSI_PROBE_PID:-}" ] && kill "$PSI_PROBE_PID" 2>/dev/null || true
    [ -n "${REDIS_PID:-}" ] && kill "$REDIS_PID" 2>/dev/null || true
    pkill -f 'redis-benchmark' 2>/dev/null || true
    "$ROOT/scripts/rollback.sh" >/dev/null 2>&1 || true
    if [[ "$BENCH_RUN_CGROUP_ROOT" == "$BENCH_CGROUP_ROOT"/* ]]; then
        find "$BENCH_RUN_CGROUP_ROOT" -depth -type d -exec rmdir {} \; 2>/dev/null || true
    fi
}
trap cleanup EXIT

write_run_orders() {
    python3 - "$OUTDIR/planned_run_orders.txt" "$RUNS" "$RUN_RANDOM_SEED" "${LABELS[@]}" <<'PY'
import random
import sys
from pathlib import Path

out = Path(sys.argv[1])
runs = int(sys.argv[2])
seed = sys.argv[3]
labels = sys.argv[4:]
rng = random.Random(seed)

lines = []
for run in range(1, runs + 1):
    order = labels[:]
    rng.shuffle(order)
    lines.append(f"run-{run} " + " ".join(order))
out.write_text("\n".join(lines) + "\n", encoding="utf-8")
PY
}

run_order_for_index() {
    local run_index="$1"
    awk -v key="run-$run_index" '$1 == key { for (i=2; i<=NF; i++) printf "%s%s", $i, (i<NF ? " " : "\n") }' \
        "$OUTDIR/planned_run_orders.txt"
}

sanitize_cgroup_component() {
    printf '%s' "$1" | sed 's/[^A-Za-z0-9_.-]/_/g'
}

write_default_bench_cgroup_values() {
    local dir="$1"
    [ -w "$dir/cpu.weight" ] && echo 100 > "$dir/cpu.weight"
    [ -w "$dir/cpu.max" ] && echo max > "$dir/cpu.max"
    return 0
}

snapshot_cgroup_state() {
    local dir="$1"
    local out="$2"
    {
        printf 'path=%s\n' "$dir"
        for file in cpu.weight cpu.max cpuset.cpus cpuset.cpus.effective cpuset.mems.effective cgroup.procs; do
            if [ -r "$dir/$file" ]; then
                printf '%s=' "$file"
                tr '\n' ' ' < "$dir/$file"
                printf '\n'
            else
                printf '%s=unavailable\n' "$file"
            fi
        done
    } > "$out"
}

prepare_bench_cgroups() {
    local rundir="$1"
    local label="$2"
    local run_name label_name group_root fg bg
    run_name="$(sanitize_cgroup_component "$(basename "$rundir")")"
    label_name="$(sanitize_cgroup_component "$label")"
    group_root="$BENCH_RUN_CGROUP_ROOT/$run_name/$label_name"
    fg="$group_root/foreground"
    bg="$group_root/background"

    mkdir -p "$fg" "$bg"
    write_default_bench_cgroup_values "$fg"
    write_default_bench_cgroup_values "$bg"
    if [ -n "${REDIS_PID:-}" ] && [ -d "/proc/$REDIS_PID" ]; then
        echo "$REDIS_PID" > "$fg/cgroup.procs" 2>/dev/null || true
    fi
    snapshot_cgroup_state "$fg" "$rundir/${label}_foreground_cgroup_pre.env"
    snapshot_cgroup_state "$bg" "$rundir/${label}_background_cgroup_pre.env"
    CASE_FOREGROUND_CGROUP="$fg"
    CASE_BACKGROUND_CGROUP="$bg"
}

finish_bench_cgroup_snapshots() {
    local rundir="$1"
    local label="$2"
    [ -n "${CASE_FOREGROUND_CGROUP:-}" ] && snapshot_cgroup_state "$CASE_FOREGROUND_CGROUP" "$rundir/${label}_foreground_cgroup_post.env"
    [ -n "${CASE_BACKGROUND_CGROUP:-}" ] && snapshot_cgroup_state "$CASE_BACKGROUND_CGROUP" "$rundir/${label}_background_cgroup_post.env"
}

start_psi_redis_probe() {
    local rundir="$1"
    local label="$2"
    (
        trap 'kill "${child:-0}" 2>/dev/null || true; exit 0' TERM INT
        while :; do
            redis-benchmark -h 127.0.0.1 -p "$REDIS_PORT" \
                -t "$PSI_PROBE_TESTS" \
                -c "$PSI_PROBE_CLIENTS" \
                -n "$PSI_PROBE_REQUESTS" \
                --csv >> "$rundir/${label}_psi_probe.csv" \
                2>> "$rundir/${label}_psi_probe.err" &
            child=$!
            wait "$child" || true
        done
    ) &
    PSI_PROBE_PID=$!
}

stop_psi_redis_probe() {
    [ -n "${PSI_PROBE_PID:-}" ] && kill "$PSI_PROBE_PID" 2>/dev/null || true
    [ -n "${PSI_PROBE_PID:-}" ] && wait "$PSI_PROBE_PID" 2>/dev/null || true
    unset PSI_PROBE_PID
}

start_stress() {
    local rundir="$1"
    local label="$2"
    local cgroup_path="${3:-}"
    if [ "$STRESS_WORKERS" -le 0 ]; then
        printf 'stress_skipped_workers=0\n' > "$rundir/${label}_stress.log"
        return 0
    fi
    if [ -n "$cgroup_path" ]; then
        (
            echo "$BASHPID" > "$cgroup_path/cgroup.procs" 2>> "$rundir/${label}_stress.log" || exit 1
            exec stress-ng --cpu "$STRESS_WORKERS" --timeout 20s
        ) > "$rundir/${label}_stress.log" 2>&1 &
    else
        stress-ng --cpu "$STRESS_WORKERS" --timeout 20s > "$rundir/${label}_stress.log" 2>&1 &
    fi
    STRESS_PID=$!
    sleep 1
}

stop_stress() {
    [ -n "${STRESS_PID:-}" ] && wait "$STRESS_PID" 2>/dev/null || true
    unset STRESS_PID
}

read_cpu_stat() {
    awk '/^cpu / { total=0; for (i=2; i<=NF; i++) total += $i; idle=$5+$6; print total, idle; }' /proc/stat
}

write_cpu_usage() {
    local rundir="$1"
    local label="$2"
    local before="$3"
    local after="$4"
    local requests="$5"
    local before_total before_idle after_total after_idle
    read -r before_total before_idle <<< "$before"
    read -r after_total after_idle <<< "$after"
    local total_delta=$((after_total - before_total))
    local idle_delta=$((after_idle - before_idle))
    local busy_delta=$((total_delta - idle_delta))
    awk -v total="$total_delta" -v idle="$idle_delta" -v busy="$busy_delta" -v requests="$requests" '
        BEGIN {
            ratio = total > 0 ? busy / total : 0;
            per10k = requests > 0 ? busy / requests * 10000 : 0;
            printf("cpu_total_delta=%d\n", total);
            printf("cpu_idle_delta=%d\n", idle);
            printf("cpu_busy_delta=%d\n", busy);
            printf("cpu_busy_ratio=%.6f\n", ratio);
            printf("cpu_per_10k_requests=%.6f\n", per10k);
            printf("cpu_metric_scope=system_proc_stat\n");
            printf("cpu_metric_warning=auxiliary_only_not_target_cgroup\n");
            printf("requests_total=%d\n", requests);
        }' > "$rundir/${label}_cpu_usage.env"
}

run_benchmark() {
    local rundir="$1"
    local label="$2"
    local cpu_before
    cpu_before="$(read_cpu_stat)"
    redis-benchmark -h 127.0.0.1 -p "$REDIS_PORT" -c "$BENCH_CLIENTS" -n "$BENCH_REQUESTS" \
        --csv > "$rundir/${label}_redis_benchmark.csv"
    awk -f "$ROOT/scripts/extract_redis_metrics.awk" \
        "$rundir/${label}_redis_benchmark.csv" > "$rundir/${label}_summary.csv"
    local row_count
    row_count="$(tail -n +2 "$rundir/${label}_summary.csv" | wc -l)"
    write_cpu_usage "$rundir" "$label" "$cpu_before" "$(read_cpu_stat)" "$((BENCH_REQUESTS * row_count))"
}

run_benchmark_with_snapshot() {
    local rundir="$1"
    local label="$2"
    local active_flag="${3:-}"
    local duration_s="${4:-2}"
    local warmup_cycles="${5:-0}"
    local backend="${6:-cgroup_v2}"

    local cpu_before
    cpu_before="$(read_cpu_stat)"
    redis-benchmark -h 127.0.0.1 -p "$REDIS_PORT" -c "$BENCH_CLIENTS" -n "$BENCH_REQUESTS" \
        --csv > "$rundir/${label}_redis_benchmark.csv" &
    local bench_pid=$!

    sleep "$SNAPSHOT_DELAY"
    "$ROOT/scripts/capture_agent_snapshot.sh" "$rundir/${label}_agent_snapshot.txt" "$INTERVAL_MS" "$active_flag" "$duration_s" "$warmup_cycles" "$backend"
    wait "$bench_pid"

    awk -f "$ROOT/scripts/extract_redis_metrics.awk" \
        "$rundir/${label}_redis_benchmark.csv" > "$rundir/${label}_summary.csv"
    local row_count
    row_count="$(tail -n +2 "$rundir/${label}_summary.csv" | wc -l)"
    write_cpu_usage "$rundir" "$label" "$cpu_before" "$(read_cpu_stat)" "$((BENCH_REQUESTS * row_count))"
}

write_group_snapshot() {
    local rundir="$1"
    local label="$2"
    cat /sys/kernel/sched_ext/state > "$rundir/${label}_sched_ext_state.txt" 2>/dev/null || true
    cat /sys/kernel/sched_ext/enable_seq > "$rundir/${label}_sched_ext_enable_seq.txt" 2>/dev/null || true
    cat /sys/kernel/sched_ext/nr_rejected > "$rundir/${label}_sched_ext_nr_rejected.txt" 2>/dev/null || true
    "$SCX_BIN" --gate-status > "$rundir/${label}_gate_status.txt" 2>&1 || true
    python3 "$ROOT/scripts/collect_scx_stats.py" "$rundir/${label}_scx_stats.json" 2>/dev/null || true
    bpftool map dump pinned /sys/fs/bpf/eulerpilot/scx_eulerpilot/v1/class_map > "$rundir/${label}_class_map_dump.json" 2>/dev/null || \
        bpftool map dump pinned /sys/fs/bpf/class_map > "$rundir/${label}_class_map_dump.json" 2>/dev/null || true
    cp /tmp/eulerpilot-psi-gate-trace.jsonl "$rundir/${label}_psi_gate_trace.jsonl" 2>/dev/null || true
    cp /tmp/eulerpilot-scx-session.log "$rundir/${label}_scx_session.log" 2>/dev/null || true
}

validate_case_output() {
    local rundir="$1"
    local label="$2"
    local snapshot="$rundir/${label}_agent_snapshot.txt"

    case "$label" in
        noisy_cgroup_v2)
            if ! grep -Eq 'assigned' "$snapshot" 2>/dev/null; then
                printf 'invalid run: missing cgroup_v2 assigned evidence in %s\n' "$snapshot" >&2
                return 1
            fi
            ;;
        quiet_scx_normal|noisy_scx_normal|noisy_scx_always_active|noisy_scx_psi)
            if ! grep -Eq 'preferred_backend: sched_ext|executor=sched_ext|backend:[[:space:]]+sched_ext' "$snapshot" 2>/dev/null; then
                printf 'invalid run: missing sched_ext evidence in %s\n' "$snapshot" >&2
                return 1
            fi
            ;;
    esac

    if [ "$label" = "noisy_scx_psi" ]; then
        if [ "$STRESS_WORKERS" -le 0 ]; then
            printf 'psi-active-not-required-with-zero-stress-workers\n' > "$rundir/${label}_psi_gate_note.txt"
            return 0
        fi
        local trace="$rundir/${label}_psi_gate_trace.jsonl"
        local gate_status="$rundir/${label}_gate_status.txt"
        if [ -f "$trace" ]; then
            if ! grep -q '"next_state":"ACTIVE"' "$trace" 2>/dev/null; then
                if ! grep -Eq 'scx-class-map-updated|gate-state-map-updated' "$snapshot" 2>/dev/null; then
                    printf 'invalid run: psi mode never entered ACTIVE and no gate update evidence in %s\n' "$trace" >&2
                    return 1
                fi
                printf 'psi-active-not-observed-in-short-compare\n' > "$rundir/${label}_psi_gate_note.txt"
            fi
        else
            if ! grep -q 'gate_state=2' "$gate_status" 2>/dev/null; then
                printf 'invalid run: missing psi trace and gate_state is not ACTIVE in %s\n' "$gate_status" >&2
                return 1
            fi
            if ! grep -Eq 'scx-class-map-updated|gate-state-map-updated' "$snapshot" 2>/dev/null; then
                printf 'invalid run: missing psi trace and no sched_ext apply evidence in %s\n' "$snapshot" >&2
                return 1
            fi
        fi
    fi

    return 0
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
    if [ "$state" != "$expected" ]; then
        printf 'invalid preflight: sched_ext state expected=%s actual=%s\n' "$expected" "$state" >&2
        return 1
    fi
    return 0
}

filter_run_order() {
    local order="$1"
    local result=()
    local candidate label
    for candidate in $order; do
        for label in "${LABELS[@]}"; do
            if [ "$candidate" = "$label" ]; then
                result+=("$candidate")
                break
            fi
        done
    done
    printf '%s\n' "${result[*]}"
}

run_case() {
    local rundir="$1"
    local label="$2"

    rm -f /tmp/eulerpilot-psi-gate-trace.jsonl /tmp/eulerpilot-scx-session.log /tmp/eulerpilot-scx.log
    "$ROOT/scripts/rollback.sh" > "$rundir/${label}_rollback_before.log" 2>&1 || true
    sleep 1
    prepare_bench_cgroups "$rundir" "$label"

    case "$label" in
        quiet_default)
            assert_sched_ext_state "disabled"
            run_benchmark_with_snapshot "$rundir" "$label" "" 2 0 cgroup_v2
            ;;
        quiet_scx_normal)
            assert_sched_ext_state "disabled"
            EULERPILOT_GATE_MODE=normal EULERPILOT_SCX_BINARY="$SCX_BIN" \
                run_benchmark_with_snapshot "$rundir" "$label" --active 4 0 sched_ext
            assert_sched_ext_state "disabled"
            ;;
        noisy_default)
            start_stress "$rundir" "$label" "$CASE_BACKGROUND_CGROUP"
            assert_sched_ext_state "disabled"
            run_benchmark_with_snapshot "$rundir" "$label" "" 2 0 cgroup_v2
            stop_stress
            ;;
        noisy_cgroup_v2)
            "$ROOT/scripts/setup_cgroup_v2.sh" > "$rundir/${label}_setup.log" 2>&1 || true
            start_stress "$rundir" "$label" "$CASE_BACKGROUND_CGROUP"
            assert_sched_ext_state "disabled"
            run_benchmark_with_snapshot "$rundir" "$label" --active 6 2 cgroup_v2
            stop_stress
            ;;
        noisy_scx_normal|noisy_scx_always_active|noisy_scx_psi)
            local gate_mode="normal"
            if [ "$label" = "noisy_scx_always_active" ]; then
                gate_mode="always-active"
            elif [ "$label" = "noisy_scx_psi" ]; then
                gate_mode="psi"
            fi
            start_stress "$rundir" "$label" "$CASE_BACKGROUND_CGROUP"
            assert_sched_ext_state "disabled"
            if [ "$label" = "noisy_scx_psi" ]; then
                start_psi_redis_probe "$rundir" "$label"
                sleep "$SNAPSHOT_DELAY"
            fi
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
                run_benchmark_with_snapshot "$rundir" "$label" --active 6 "$warmup_cycles" sched_ext
            if [ "$label" = "noisy_scx_psi" ]; then
                stop_psi_redis_probe
            fi
            assert_sched_ext_state "disabled"
            stop_stress
            ;;
        *)
            printf 'unknown label: %s\n' "$label" >&2
            return 1
            ;;
    esac

    finish_bench_cgroup_snapshots "$rundir" "$label"
    write_group_snapshot "$rundir" "$label"
    if ! validate_case_output "$rundir" "$label"; then
        mark_invalid_run "$rundir" "$label" "validation-failed"
        return 1
    fi
}

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

redis-server "$OUTDIR/redis.conf" > /dev/null 2>&1 &
REDIS_PID=$!
sleep 2

printf '[INFO] Redis sched_ext compare output: %s\n' "$OUTDIR"
write_run_orders

declare -A SUMMARY_PATHS
for run in $(seq 1 "$RUNS"); do
    local_rundir="$OUTDIR/run-$run"
    mkdir -p "$local_rundir"
    printf '[INFO] run %s/%s\n' "$run" "$RUNS"
    run_order="$(filter_run_order "$(run_order_for_index "$run")")"
    printf '%s\n' "$run_order" > "$local_rundir/run_order.txt"
    for label in $run_order; do
        run_case "$local_rundir" "$label"
        SUMMARY_PATHS["$label"]+="$local_rundir/${label}_summary.csv"$'\n'
        sleep 2
    done
done

python3 - <<'PY' "$OUTDIR" "$OUTDIR/run_manifest.json" "$RUNS" "$REDIS_PORT" "$BENCH_CLIENTS" "$BENCH_REQUESTS" "$STRESS_WORKERS" "${LABELS[@]}"
import json, os, sys, subprocess
from pathlib import Path

result_dir = Path(sys.argv[1])
out = Path(sys.argv[2])
runs = int(sys.argv[3])
redis_port = sys.argv[4]
bench_clients = sys.argv[5]
bench_requests = sys.argv[6]
stress_workers = sys.argv[7]
labels = sys.argv[8:]

label_titles = {
    "quiet_default": "仅 Redis，默认调度器",
    "quiet_scx_normal": "仅 Redis，sched_ext 常驻但保持 normal",
    "noisy_default": "Redis + stress-ng，默认调度器",
    "noisy_cgroup_v2": "Redis + stress-ng，cgroup v2 控制",
    "noisy_scx_normal": "Redis + stress-ng，sched_ext normal",
    "noisy_scx_always_active": "Redis + stress-ng，sched_ext always-active",
    "noisy_scx_psi": "Redis + stress-ng，sched_ext psi",
}

manifest = {
    "run_id": out.parent.name,
    "timestamp": subprocess.check_output(["date", "--iso-8601=seconds"], text=True).strip(),
    "host": subprocess.check_output(["hostname"], text=True).strip(),
    "kernel_release": subprocess.check_output(["uname", "-r"], text=True).strip(),
    "runs": runs,
    "redis_port": redis_port,
    "bench_clients": bench_clients,
    "bench_requests": bench_requests,
    "stress_workers": stress_workers,
    "sched_ext_switch_mode": "full",
    "labels": labels,
    "label_titles": label_titles,
    "randomization": {
        "scheme": "randomized_complete_block",
        "seed": os.environ.get("RUN_RANDOM_SEED", ""),
        "planned_run_orders_file": str(result_dir / "planned_run_orders.txt")
    },
    "benchmark_cgroup_baseline": {
        "root": os.environ.get("BENCH_RUN_CGROUP_ROOT", ""),
        "default_noisy_weight": 100,
        "default_noisy_cpu_max": "max",
        "cpuset_control": "disabled"
    },
    "formal_artifact": {
        "tested_code_commit": os.environ.get("EULERPILOT_TESTED_CODE_COMMIT", ""),
        "artifact_id": os.environ.get("EULERPILOT_ARTIFACT_ID", ""),
        "build_attempt_id": os.environ.get("EULERPILOT_BUILD_ATTEMPT_ID", ""),
        "build_manifest_sha256": os.environ.get("EULERPILOT_BUILD_MANIFEST_SHA256", ""),
        "artifact_manifest": os.environ.get("EULERPILOT_ARTIFACT_MANIFEST", ""),
        "artifact_dir": os.environ.get("EULERPILOT_ARTIFACT_DIR", ""),
        "agent_binary_sha256": os.environ.get("EULERPILOT_AGENT_SHA256", ""),
        "scx_binary_sha256": os.environ.get("EULERPILOT_SCX_SHA256", ""),
        "observer_bpf_sha256": os.environ.get("EULERPILOT_OBSERVER_BPF_SHA256", ""),
        "scx_bpf_sha256": os.environ.get("EULERPILOT_SCX_BPF_SHA256", ""),
    },
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

python3 "$ROOT/scripts/redis_compare_summary.py" "$OUTDIR/run_manifest.json" "$OUTDIR/compare_summary_avg.csv"
python3 "$ROOT/scripts/render_redis_backend_compare_report.py" \
    "$OUTDIR/compare_summary_avg.csv" \
    "$OUTDIR/run_manifest.json" \
    "$OUTDIR/report.md"

cat > "$OUTDIR/summary.md" <<EOF
# Redis sched_ext 正式对照

- timestamp: $(date --iso-8601=seconds)
- runs: $RUNS
- redis port: $REDIS_PORT
- bench clients: $BENCH_CLIENTS
- bench requests: $BENCH_REQUESTS
- stress workers: $STRESS_WORKERS
- scx bin: $SCX_BIN

本目录包含：

- \`run_manifest.json\`
- \`compare_summary_avg.csv\`
- \`report.md\`
- \`run-*/<label>_summary.csv\`
- \`run-*/<label>_agent_snapshot.txt\`
- \`run-*/<label>_scx_stats.json\`
- \`run-*/<label>_gate_status.txt\`
- \`run-*/run_order.txt\`
- \`planned_run_orders.txt\`
- \`run-*/<label>_foreground_cgroup_pre.env\` / \`post.env\`
- \`run-*/<label>_background_cgroup_pre.env\` / \`post.env\`
- \`run-*/<label>_invalid_reason.txt\`（仅在该组失效时出现）

口径说明：

- \`cpu_per_10k_requests\` 来自同窗口全系统 \`/proc/stat\`，对应 env 字段为 \`cpu_metric_scope=system_proc_stat\`，只作为辅助效率指标，不作为目标 cgroup CPU 消耗结论。
- \`noisy_default\` 使用 \`/sys/fs/cgroup/eulerpilot-bench/<run-id>\` 下的临时前台/后台对等 cgroup，前后台均保持 \`cpu.weight=100\`、\`cpu.max=max\`，不启用 EulerPilot policy，不再使用 \`/sys/fs/cgroup/eulerpilot/background\` 作为默认基线。
- 执行顺序使用 randomized complete block，随机种子写入 \`run_manifest.json\`，实际顺序写入每轮 \`run_order.txt\`。
- \`noisy_scx_psi\` 会额外运行 Redis PSI probe 以稳定触发 PSI gate，适合作为门控/调度路径证据；净性能对比应优先看无额外 probe 的 default/cgroup/scx-normal 组。
EOF

printf '[INFO] Redis sched_ext compare complete: %s\n' "$OUTDIR"
