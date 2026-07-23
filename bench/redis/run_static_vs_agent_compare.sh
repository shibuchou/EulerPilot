#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="${ROOT:-$(cd "$SCRIPT_DIR/../.." && pwd)}"
STAMP="$(date +%Y%m%d-%H%M%S)"
OUTDIR="${OUTDIR:-$ROOT/results/final/redis-static-vs-agent-$STAMP}"
REDIS_PORT="${REDIS_PORT:-6387}"
RUNS="${RUNS:-5}"
STRESS_WORKERS="${STRESS_WORKERS:-2}"
BENCH_CLIENTS="${BENCH_CLIENTS:-16}"
BENCH_REQUESTS="${BENCH_REQUESTS:-20000}"
INTERVAL_MS="${INTERVAL_MS:-1000}"
SNAPSHOT_DELAY="${SNAPSHOT_DELAY:-0.2}"
BACKGROUND_CGROUP="/sys/fs/cgroup/eulerpilot/background"

mkdir -p "$OUTDIR"
OUTDIR="$(cd "$OUTDIR" && pwd)"

cleanup() {
    [ -n "${STRESS_PID:-}" ] && kill "$STRESS_PID" 2>/dev/null || true
    [ -n "${REDIS_PID:-}" ] && kill "$REDIS_PID" 2>/dev/null || true
    pkill -f 'redis-benchmark' 2>/dev/null || true
    "$ROOT/scripts/rollback.sh" >/dev/null 2>&1 || true
}
trap cleanup EXIT

read_cpu_stat() {
    awk '/^cpu / { total=0; for (i=2; i<=NF; i++) total += $i; idle=$5+$6; print total, idle; }' /proc/stat
}

read_cpu_stat_file() {
    local file="$1"
    local key="$2"
    awk -v key="$key" '$1 == key { print $2; found=1 } END { if (!found) print 0 }' "$file" 2>/dev/null || echo 0
}

snapshot_background_cpu() {
    local out="$1"
    if [ -r "$BACKGROUND_CGROUP/cpu.stat" ]; then
        cp "$BACKGROUND_CGROUP/cpu.stat" "$out"
    else
        : > "$out"
    fi
}

append_invalid_reason() {
    local file="$1"
    local reason="$2"
    printf '%s\n' "$reason" >> "$file"
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

snapshot_pid_cgroups() {
    local pid_file="$1"
    local out="$2"
    : > "$out"
    while IFS= read -r pid; do
        [ -n "$pid" ] || continue
        if [ -r "/proc/$pid/cgroup" ]; then
            while IFS= read -r line; do
                printf '%s %s\n' "$pid" "$line" >> "$out"
            done < "/proc/$pid/cgroup"
        else
            printf '%s missing-proc-cgroup\n' "$pid" >> "$out"
        fi
    done < "$pid_file"
}

validate_controlled_pids_in_cgroup() {
    local rundir="$1"
    local label="$2"
    local expected="$3"
    local invalid="$rundir/${label}_invalid_reason.txt"
    local pid_file="$rundir/${label}_controlled_pids.txt"
    local cgroup_file="$rundir/${label}_controlled_pid_cgroups.txt"
    local expected_suffix="${expected#/sys/fs/cgroup}"
    local pid_count=0
    local missing=0
    local outside=0

    if [ ! -s "$pid_file" ]; then
        append_invalid_reason "$invalid" "missing-controlled-pids"
        return 1
    fi
    if [ ! -s "$cgroup_file" ]; then
        snapshot_pid_cgroups "$pid_file" "$cgroup_file"
    fi
    pid_count="$(awk 'NF { count++ } END { print count + 0 }' "$pid_file")"
    if [ "$pid_count" -lt "$((STRESS_WORKERS + 1))" ]; then
        append_invalid_reason "$invalid" "controlled-pid-count-too-low:$pid_count"
    fi
    missing="$(grep -c 'missing-proc-cgroup' "$cgroup_file" 2>/dev/null || true)"
    if [ "$missing" -gt 0 ]; then
        append_invalid_reason "$invalid" "controlled-pid-cgroup-missing:$missing"
    fi
    outside="$(awk -v expected="$expected_suffix" '
        $2 == "missing-proc-cgroup" { next }
        {
            split($2, fields, ":");
            path = fields[3];
            if (path != expected) {
                outside++;
            }
        }
        END { print outside + 0 }
    ' "$cgroup_file")"
    if [ "$outside" -gt 0 ]; then
        append_invalid_reason "$invalid" "controlled-pid-outside-background-cgroup:$outside"
    fi
    [ ! -s "$invalid" ]
}

start_stress() {
    local rundir="$1"
    local label="$2"
    local cgroup_path="${3:-}"
    if [ -n "$cgroup_path" ]; then
        (
            if ! echo "$BASHPID" > "$cgroup_path/cgroup.procs" 2>> "$rundir/${label}_stress.log"; then
                printf 'failed_to_enter_cgroup=%s\n' "$cgroup_path" >> "$rundir/${label}_stress.log"
                exit 1
            fi
            exec stress-ng --cpu "$STRESS_WORKERS" --timeout 20s
        ) > "$rundir/${label}_stress.log" 2>&1 &
    else
        stress-ng --cpu "$STRESS_WORKERS" --timeout 20s > "$rundir/${label}_stress.log" 2>&1 &
    fi
    STRESS_PID=$!
    sleep 1
    snapshot_stress_pids "$STRESS_PID" "$rundir/${label}_controlled_pids.txt"
    snapshot_pid_cgroups "$rundir/${label}_controlled_pids.txt" "$rundir/${label}_controlled_pid_cgroups.txt"
    cat "$BACKGROUND_CGROUP/cgroup.procs" > "$rundir/${label}_background_cgroup_procs.txt" 2>/dev/null || true
}

stop_stress() {
    [ -n "${STRESS_PID:-}" ] && wait "$STRESS_PID" 2>/dev/null || true
    unset STRESS_PID
}

wait_for_redis() {
    for _ in $(seq 1 20); do
        if redis-cli -h 127.0.0.1 -p "$REDIS_PORT" ping >/dev/null 2>&1; then
            return 0
        fi
        sleep 0.2
    done
    printf '[ERROR] redis-server did not become ready on port %s\n' "$REDIS_PORT" >&2
    sed 's/^/[redis] /' "$OUTDIR/redis.log" >&2 2>/dev/null || true
    return 1
}

snapshot_stress_pids() {
    local root_pid="$1"
    local out="$2"
    : > "$out"
    [ -n "$root_pid" ] || return 0
    {
        printf '%s\n' "$root_pid"
        pgrep -P "$root_pid" 2>/dev/null || true
    } | awk 'NF && !seen[$1]++ { print }' > "$out"
}

run_redis_benchmark() {
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

run_agent_benchmark() {
    local rundir="$1"
    local label="$2"
    local cpu_before
    cpu_before="$(read_cpu_stat)"
    redis-benchmark -h 127.0.0.1 -p "$REDIS_PORT" -c "$BENCH_CLIENTS" -n "$BENCH_REQUESTS" \
        --csv > "$rundir/${label}_redis_benchmark.csv" &
    local bench_pid=$!
    sleep "$SNAPSHOT_DELAY"
    "$ROOT/scripts/capture_agent_snapshot.sh" "$rundir/${label}_agent_snapshot.txt" "$INTERVAL_MS" --active 6 2 cgroup_v2
    wait "$bench_pid"
    awk -f "$ROOT/scripts/extract_redis_metrics.awk" \
        "$rundir/${label}_redis_benchmark.csv" > "$rundir/${label}_summary.csv"
    local row_count
    row_count="$(tail -n +2 "$rundir/${label}_summary.csv" | wc -l)"
    write_cpu_usage "$rundir" "$label" "$cpu_before" "$(read_cpu_stat)" "$((BENCH_REQUESTS * row_count))"
}

run_agent_observe_benchmark() {
    local rundir="$1"
    local label="$2"
    local cpu_before
    cpu_before="$(read_cpu_stat)"
    redis-benchmark -h 127.0.0.1 -p "$REDIS_PORT" -c "$BENCH_CLIENTS" -n "$BENCH_REQUESTS" \
        --csv > "$rundir/${label}_redis_benchmark.csv" &
    local bench_pid=$!
    sleep "$SNAPSHOT_DELAY"
    "$ROOT/scripts/capture_agent_snapshot.sh" "$rundir/${label}_agent_snapshot.txt" "$INTERVAL_MS" observe-only 6 2 cgroup_v2
    wait "$bench_pid"
    awk -f "$ROOT/scripts/extract_redis_metrics.awk" \
        "$rundir/${label}_redis_benchmark.csv" > "$rundir/${label}_summary.csv"
    local row_count
    row_count="$(tail -n +2 "$rundir/${label}_summary.csv" | wc -l)"
    write_cpu_usage "$rundir" "$label" "$cpu_before" "$(read_cpu_stat)" "$((BENCH_REQUESTS * row_count))"
}

run_case() {
    local rundir="$1"
    local label="$2"
    "$ROOT/scripts/rollback.sh" > "$rundir/${label}_rollback_before.log" 2>&1 || true
    sleep 1
    snapshot_background_cpu "$rundir/${label}_cpu_stat_before.txt"

    case "$label" in
        default_noisy)
            "$ROOT/scripts/setup_cgroup_v2.sh" > "$rundir/${label}_setup.log" 2>&1
            start_stress "$rundir" "$label" "$BACKGROUND_CGROUP"
            run_redis_benchmark "$rundir" "$label"
            stop_stress
            ;;
        agent_observe_only)
            "$ROOT/scripts/setup_cgroup_v2.sh" > "$rundir/${label}_setup.log" 2>&1
            start_stress "$rundir" "$label" "$BACKGROUND_CGROUP"
            run_agent_observe_benchmark "$rundir" "$label"
            stop_stress
            ;;
        manual_static)
            "$ROOT/scripts/setup_cgroup_v2.sh" > "$rundir/${label}_setup.log" 2>&1
            echo "10000 100000" > "$BACKGROUND_CGROUP/cpu.max"
            start_stress "$rundir" "$label" "$BACKGROUND_CGROUP"
            cat "$BACKGROUND_CGROUP/cpu.max" > "$rundir/${label}_cpu_max.txt"
            run_redis_benchmark "$rundir" "$label"
            stop_stress
            ;;
        agent_dynamic)
            "$ROOT/scripts/setup_cgroup_v2.sh" > "$rundir/${label}_setup.log" 2>&1
            start_stress "$rundir" "$label" "$BACKGROUND_CGROUP"
            run_agent_benchmark "$rundir" "$label"
            stop_stress
            ;;
        *)
            printf 'unknown case: %s\n' "$label" >&2
            return 1
            ;;
    esac

    snapshot_background_cpu "$rundir/${label}_cpu_stat_after.txt"
    local before="$rundir/${label}_cpu_stat_before.txt"
    local after="$rundir/${label}_cpu_stat_after.txt"
    local before_throttled after_throttled before_usec after_usec
    before_throttled="$(read_cpu_stat_file "$before" nr_throttled)"
    after_throttled="$(read_cpu_stat_file "$after" nr_throttled)"
    before_usec="$(read_cpu_stat_file "$before" throttled_usec)"
    after_usec="$(read_cpu_stat_file "$after" throttled_usec)"
    {
        printf 'nr_throttled_delta=%d\n' "$((after_throttled - before_throttled))"
        printf 'throttled_usec_delta=%d\n' "$((after_usec - before_usec))"
    } > "$rundir/${label}_throttle.env"

    local invalid="$rundir/${label}_invalid_reason.txt"
    rm -f "$invalid"
    if [ "$label" = "default_noisy" ] || [ "$label" = "agent_observe_only" ] ||
       [ "$label" = "manual_static" ] || [ "$label" = "agent_dynamic" ]; then
        validate_controlled_pids_in_cgroup "$rundir" "$label" "$BACKGROUND_CGROUP" || true
    fi
    if [ "$label" = "manual_static" ]; then
        if [ ! -f "$rundir/${label}_cpu_max.txt" ] ||
           [ "$(cat "$rundir/${label}_cpu_max.txt" 2>/dev/null)" != "10000 100000" ]; then
            append_invalid_reason "$invalid" "manual-static-cpu-max-not-applied"
        fi
        if [ "$((after_throttled - before_throttled))" -le 0 ]; then
            append_invalid_reason "$invalid" "manual-static-no-throttling"
        fi
    fi
    if [ "$label" = "agent_dynamic" ]; then
        if [ ! -s "$rundir/${label}_agent_snapshot.txt" ]; then
            append_invalid_reason "$invalid" "agent-dynamic-missing-agent-snapshot"
        fi
    fi
    if [ "$label" = "agent_observe_only" ]; then
        if [ ! -s "$rundir/${label}_agent_snapshot.txt" ]; then
            append_invalid_reason "$invalid" "agent-observe-only-missing-agent-snapshot"
        fi
    fi
    if [ -s "$invalid" ]; then
        printf '[ERROR] invalid %s/%s:\n' "$(basename "$rundir")" "$label" >&2
        sed 's/^/[ERROR]   /' "$invalid" >&2
        return 1
    fi
    rm -f "$invalid"
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
wait_for_redis

printf '[INFO] Redis static-vs-agent output: %s\n' "$OUTDIR"
for run in $(seq 1 "$RUNS"); do
    rundir="$OUTDIR/run-$run"
    mkdir -p "$rundir"
    printf '[INFO] run %s/%s\n' "$run" "$RUNS"
    for label in default_noisy agent_observe_only manual_static agent_dynamic; do
        run_case "$rundir" "$label"
        sleep 2
    done
done

python3 - <<'PY' "$OUTDIR" "$OUTDIR/compare_summary_avg.csv" "$OUTDIR/report.md" "$RUNS" "$STRESS_WORKERS"
import csv
import statistics
import sys
from collections import defaultdict
from pathlib import Path

root = Path(sys.argv[1])
summary_csv = Path(sys.argv[2])
report_md = Path(sys.argv[3])
runs = sys.argv[4]
stress_workers = sys.argv[5]
labels = ["default_noisy", "agent_observe_only", "manual_static", "agent_dynamic"]

def mean(values):
    return f"{statistics.mean(values):.3f}" if values else ""

def load_env(path):
    out = {}
    if not path.exists():
        return out
    for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
        if "=" in line:
            k, v = line.split("=", 1)
            try:
                out[k] = float(v)
            except ValueError:
                out[k] = v
    return out

bucket = defaultdict(lambda: defaultdict(list))
tests = set()
for run_dir in sorted(root.glob("run-*")):
    for label in labels:
        summary = run_dir / f"{label}_summary.csv"
        if not summary.exists():
            continue
        cpu = load_env(run_dir / f"{label}_cpu_usage.env")
        throttle = load_env(run_dir / f"{label}_throttle.env")
        with summary.open(newline="", encoding="utf-8") as f:
            for row in csv.DictReader(f):
                test = row["test"]
                tests.add(test)
                bucket[test][f"{label}_rps"].append(float(row["rps"]))
                bucket[test][f"{label}_p99_ms"].append(float(row["p99_latency_ms"]))
                bucket[test][f"{label}_cpu_per_10k_requests"].append(float(cpu.get("cpu_per_10k_requests", 0.0)))
                bucket[test][f"{label}_cpu_busy_ratio"].append(float(cpu.get("cpu_busy_ratio", 0.0)))
                bucket[test][f"{label}_nr_throttled_delta"].append(float(throttle.get("nr_throttled_delta", 0.0)))
                bucket[test][f"{label}_throttled_usec_delta"].append(float(throttle.get("throttled_usec_delta", 0.0)))

fieldnames = ["test"]
for label in labels:
    fieldnames.extend([
        f"{label}_rps_avg",
        f"{label}_p99_ms_avg",
        f"{label}_cpu_per_10k_requests_avg",
        f"{label}_cpu_busy_ratio_avg",
        f"{label}_nr_throttled_delta_avg",
        f"{label}_throttled_usec_delta_avg",
    ])

rows = []
for test in sorted(tests):
    row = {"test": test}
    for label in labels:
        row[f"{label}_rps_avg"] = mean(bucket[test][f"{label}_rps"])
        row[f"{label}_p99_ms_avg"] = mean(bucket[test][f"{label}_p99_ms"])
        row[f"{label}_cpu_per_10k_requests_avg"] = mean(bucket[test][f"{label}_cpu_per_10k_requests"])
        row[f"{label}_cpu_busy_ratio_avg"] = mean(bucket[test][f"{label}_cpu_busy_ratio"])
        row[f"{label}_nr_throttled_delta_avg"] = mean(bucket[test][f"{label}_nr_throttled_delta"])
        row[f"{label}_throttled_usec_delta_avg"] = mean(bucket[test][f"{label}_throttled_usec_delta"])
    rows.append(row)

with summary_csv.open("w", newline="", encoding="utf-8") as f:
    writer = csv.DictWriter(f, fieldnames=fieldnames)
    writer.writeheader()
    writer.writerows(rows)

lines = [
    "# Redis 人工静态 vs Agent 动态对比",
    "",
    f"- 结果目录：`{root}`",
    f"- 轮数：`{runs}`",
    f"- stress workers：`{stress_workers}`",
    "",
    "## 组别",
    "",
    "- `default_noisy`：Redis + stress-ng，干扰线程进入 background cgroup，但不写限额、不启动 Agent。",
    "- `agent_observe_only`：Redis + stress-ng，干扰线程进入 background cgroup，启动 EulerPilot observe-only，不执行控制动作。",
    "- `manual_static`：在后台 cgroup 内启动 stress-ng 及其 worker，手动写入 `cpu.max=10000 100000`，不启动 Agent。",
    "- `agent_dynamic`：在同一后台 cgroup 内启动 stress-ng 及其 worker，启动 EulerPilot cgroup_v2 active，由 Agent 自动观测、决策和回滚。",
    "",
    "## 有效性门禁",
    "",
    "- 每组均保存 `controlled_pids.txt`、`controlled_pid_cgroups.txt` 与 `background_cgroup_procs.txt`。",
    "- 四组 stress-ng 父进程和 worker 必须位于同一 background cgroup。",
    "- `manual_static` 必须出现 `nr_throttled_delta > 0`，否则该轮生成 `invalid_reason.txt` 并失败退出。",
    "- `cpu_per_10k_requests` 当前来自全系统 `/proc/stat` 同窗口采样，字段 `cpu_metric_scope=system_proc_stat`，只作为辅助指标，不作为目标 cgroup CPU 消耗结论。",
    "",
    "## GET 视角核心表",
    "",
]
get = next((row for row in rows if row.get("test") == "GET"), None)
if get:
    headers = ["label", "rps_avg", "p99_ms_avg", "cpu_per_10k_requests_avg", "nr_throttled_delta_avg", "throttled_usec_delta_avg"]
    lines.append("| " + " | ".join(headers) + " |")
    lines.append("| " + " | ".join(["---"] + ["---:" for _ in headers[1:]]) + " |")
    for label in labels:
        lines.append("| " + " | ".join([
            label,
            get.get(f"{label}_rps_avg", ""),
            get.get(f"{label}_p99_ms_avg", ""),
            get.get(f"{label}_cpu_per_10k_requests_avg", ""),
            get.get(f"{label}_nr_throttled_delta_avg", ""),
            get.get(f"{label}_throttled_usec_delta_avg", ""),
        ]) + " |")
else:
    lines.append("- 未找到 GET 汇总行。")

lines.extend([
    "",
    "## 结论边界",
    "",
    "本实验用于比较同一类后台干扰线程在无控制、Agent observe-only、人工静态控制与 Agent 动态控制下的表现，并提供自动观测、审计和 rollback 证据。结果不用于声明 Agent 永远超过人工最优参数；旧版只移动 stress-ng 父 PID 的结果已撤下，必须以本脚本重跑后的输出作为有效证据。",
])
report_md.write_text("\n".join(lines) + "\n", encoding="utf-8")
PY

cat > "$OUTDIR/summary.md" <<EOF
# Redis static vs Agent dynamic compare

- timestamp: $(date --iso-8601=seconds)
- runs: $RUNS
- redis port: $REDIS_PORT
- bench clients: $BENCH_CLIENTS
- bench requests: $BENCH_REQUESTS
- stress workers: $STRESS_WORKERS

本目录包含 compare_summary_avg.csv、report.md、run-*/<label>_summary.csv、run-*/<label>_cpu_usage.env、run-*/<label>_throttle.env、run-*/<label>_controlled_pids.txt、run-*/<label>_controlled_pid_cgroups.txt 与 run-*/<label>_background_cgroup_procs.txt。若任一有效性检查失败，会生成 run-*/<label>_invalid_reason.txt 并使脚本退出。
EOF

printf '[INFO] Redis static-vs-agent complete: %s\n' "$OUTDIR"
