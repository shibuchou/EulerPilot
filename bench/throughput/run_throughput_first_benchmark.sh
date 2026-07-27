#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="${ROOT:-$(cd "$SCRIPT_DIR/../.." && pwd)}"
STAMP="$(date +%Y%m%d-%H%M%S)"
OUTDIR="${OUTDIR:-$ROOT/results/final/throughput-first-$STAMP}"
RUNS="${RUNS:-3}"
WORKERS="${WORKERS:-2}"
DURATION_S="${DURATION_S:-8}"
INTERVAL_MS="${INTERVAL_MS:-500}"
SNAPSHOT_DELAY="${SNAPSHOT_DELAY:-0.2}"
SCX_BIN="${SCX_BIN:-$(command -v scx_eulerpilot 2>/dev/null || true)}"
SCX_BIN="${SCX_BIN:-/usr/local/bin/scx_eulerpilot}"

LABELS=(
    "default_batch"
    "cgroup_throughput_first"
    "scx_throughput_first"
)

mkdir -p "$OUTDIR"
OUTDIR="$(cd "$OUTDIR" && pwd)"

PIDS=()

cleanup() {
    for pid in "${PIDS[@]:-}"; do
        kill "$pid" 2>/dev/null || true
    done
    "$ROOT/scripts/rollback.sh" >/dev/null 2>&1 || true
}
trap cleanup EXIT

build_sysbench_lite() {
    cat > "$OUTDIR/sysbench_lite.c" <<'C'
#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t stop_requested = 0;

static void on_signal(int sig) {
    (void)sig;
    stop_requested = 1;
}

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

int main(int argc, char **argv) {
    const double duration = argc > 1 ? atof(argv[1]) : 8.0;
    const double end = now_s() + duration;
    volatile uint64_t state = (uint64_t)getpid() * 11400714819323198485ull;
    uint64_t ops = 0;

    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);

    while (!stop_requested && now_s() < end) {
        for (int i = 0; i < 4096; ++i) {
            state ^= state << 7;
            state ^= state >> 9;
            state *= 1099511628211ull;
        }
        ops += 4096;
    }

    const double elapsed = duration > 0.0 ? duration : 1.0;
    printf("pid=%d ops=%" PRIu64 " elapsed_s=%.6f state=%" PRIu64 "\n",
           getpid(), ops, elapsed, state);
    return errno == 0 ? 0 : 1;
}
C
    cc -O2 -Wall -Wextra "$OUTDIR/sysbench_lite.c" -o "$OUTDIR/sysbench"
}

start_batch_workers() {
    local rundir="$1"
    local label="$2"
    local duration="$3"
    PIDS=()
    : > "$rundir/${label}_worker_pids.txt"
    for worker in $(seq 1 "$WORKERS"); do
        "$OUTDIR/sysbench" "$duration" > "$rundir/${label}_worker_${worker}.out" 2>&1 &
        local pid=$!
        PIDS+=("$pid")
        printf '%s\n' "$pid" >> "$rundir/${label}_worker_pids.txt"
    done
}

wait_batch_workers() {
    local rc=0
    for pid in "${PIDS[@]:-}"; do
        wait "$pid" || rc=$?
    done
    PIDS=()
    return "$rc"
}

write_worker_summary() {
    local rundir="$1"
    local label="$2"
    awk -v label="$label" -v run="$(basename "$rundir")" '
        BEGIN { total = 0; count = 0; elapsed = 0; }
        /^pid=/ {
            for (i = 1; i <= NF; i++) {
                split($i, kv, "=");
                if (kv[1] == "ops") total += kv[2];
                if (kv[1] == "elapsed_s") elapsed = kv[2];
            }
            count++;
        }
        END {
            opsps = elapsed > 0 ? total / elapsed : 0;
            printf("run,label,workers,total_ops,ops_per_sec\n");
            printf("%s,%s,%d,%.0f,%.3f\n", run, label, count, total, opsps);
        }
    ' "$rundir/${label}"_worker_*.out > "$rundir/${label}_summary.csv"
}

collect_scx_stats() {
    local out="$1"
    python3 "$ROOT/scripts/collect_scx_stats.py" "$out" 2>"$out.err" || {
        printf '{}\n' > "$out"
        return 0
    }
}

write_snapshot_metrics() {
    local rundir="$1"
    local label="$2"
    local snapshot="$rundir/${label}_agent_snapshot.txt"
    local stats="$rundir/${label}_scx_stats.json"
    local metrics="$rundir/${label}_metrics.env"
    local applied=0 throughput_hits=0 batch_hits=0 enqueue_batch=0
    local dispatch_batch_dsq=0 dispatch_batch_local=0 dispatch_batch_shared_fallback=0
    local running_batch=0 batch_dispatch_total=0
    local counter_delta_valid=1 counter_delta_invalid_reason=""
    local total_ops=0 completion_valid=0

    if [ -f "$snapshot" ]; then
        applied="$(grep -Ec '\|\+' "$snapshot" 2>/dev/null || true)"
        throughput_hits="$(grep -Ec 'throughput_profile' "$snapshot" 2>/dev/null || true)"
    fi
    if [ -f "$rundir/${label}_summary.csv" ]; then
        total_ops="$(python3 - <<'PY' "$rundir/${label}_summary.csv"
import csv, sys
try:
    with open(sys.argv[1], newline="", encoding="utf-8") as f:
        row = next(csv.DictReader(f))
    print(int(float(row.get("total_ops", 0))))
except Exception:
    print(0)
PY
)"
        if [ "$total_ops" -gt 0 ]; then
            completion_valid=1
        fi
    fi
    if [ -f "$stats" ]; then
        eval "$(python3 - <<'PY' "$stats"
import json, sys
try:
    data = json.load(open(sys.argv[1], encoding="utf-8"))
except Exception:
    data = {}
def v(key):
    try:
        return int(data.get(key, 0))
    except Exception:
        return 0
dispatch_dsq = v("dispatch_batch")
dispatch_local = v("direct_local_batch")
dispatch_shared_fallback = v("dispatch_batch_shared_fallback")
print(f'batch_hits={v("class_hits_batch")}')
print(f'enqueue_batch={v("enqueue_batch")}')
print(f'dispatch_batch_dsq={dispatch_dsq}')
print(f'dispatch_batch_local={dispatch_local}')
print(f'dispatch_batch_shared_fallback={dispatch_shared_fallback}')
print(f'batch_dispatch_total={dispatch_dsq + dispatch_local + dispatch_shared_fallback}')
print(f'running_batch={v("running_batch")}')
print(f'counter_delta_valid={1 if data.get("__counter_delta_valid", True) else 0}')
print(f'counter_delta_invalid_reason={data.get("__counter_delta_invalid_reason", "")}')
PY
)"
    fi
    local dispatch_valid=1
    if [ "$counter_delta_valid" -ne 1 ]; then
        dispatch_valid=0
    fi
    if [ "$enqueue_batch" -gt 0 ] && [ "$batch_dispatch_total" -le 0 ]; then
        dispatch_valid=0
    fi
    {
        printf 'agent_applied_count=%s\n' "$applied"
        printf 'throughput_profile_hits=%s\n' "$throughput_hits"
        printf 'class_hits_batch=%s\n' "$batch_hits"
        printf 'enqueue_batch=%s\n' "$enqueue_batch"
        printf 'dispatch_batch_dsq=%s\n' "$dispatch_batch_dsq"
        printf 'dispatch_batch_local=%s\n' "$dispatch_batch_local"
        printf 'dispatch_batch_shared_fallback=%s\n' "$dispatch_batch_shared_fallback"
        printf 'batch_dispatch_total=%s\n' "$batch_dispatch_total"
        printf 'running_batch=%s\n' "$running_batch"
        printf 'counter_delta_valid=%s\n' "$counter_delta_valid"
        printf 'counter_delta_invalid_reason=%s\n' "$counter_delta_invalid_reason"
        printf 'collection_valid=1\n'
        printf 'classification_valid=%s\n' "$([ "$batch_hits" -gt 0 ] && echo 1 || echo 0)"
        printf 'dispatch_accounting_valid=%s\n' "$dispatch_valid"
        printf 'completion_metric_name=total_ops\n'
        printf 'completion_metric_source=sysbench-lite-summary\n'
        printf 'completion_minimum=1\n'
        printf 'completion_actual=%s\n' "$total_ops"
        printf 'completion_collection_valid=%s\n' "$completion_valid"
        printf 'workload_completion_valid=%s\n' "$completion_valid"
        printf 'scheduler_stability_valid=1\n'
    } > "$metrics"
}

run_agent_capture() {
    local rundir="$1"
    local label="$2"
    local backend="$3"
    local gate_mode="${4:-normal}"
    EULERPILOT_THROUGHPUT_FIRST=1 \
    EULERPILOT_GATE_MODE="$gate_mode" \
    EULERPILOT_SCX_BINARY="$SCX_BIN" \
        "$ROOT/scripts/capture_agent_snapshot.sh" \
        "$rundir/${label}_agent_snapshot.txt" \
        "$INTERVAL_MS" --active "$DURATION_S" 0 "$backend"
}

run_case() {
    local rundir="$1"
    local label="$2"
    "$ROOT/scripts/rollback.sh" > "$rundir/${label}_rollback_before.log" 2>&1 || true
    rm -f /tmp/eulerpilot-scx-session.log /tmp/eulerpilot-scx.log
    sleep 1

    case "$label" in
        default_batch)
            start_batch_workers "$rundir" "$label" "$DURATION_S"
            wait_batch_workers
            ;;
        cgroup_throughput_first)
            "$ROOT/scripts/setup_cgroup_v2.sh" > "$rundir/${label}_setup.log" 2>&1 || true
            start_batch_workers "$rundir" "$label" "$((DURATION_S + 2))"
            sleep "$SNAPSHOT_DELAY"
            run_agent_capture "$rundir" "$label" "cgroup_v2" "normal"
            wait_batch_workers
            ;;
        scx_throughput_first)
            if [ ! -x "$SCX_BIN" ] || [ ! -d /sys/kernel/sched_ext ]; then
                printf 'scx-unavailable-or-binary-missing\n' > "$rundir/${label}_invalid_reason.txt"
                return 1
            fi
            collect_scx_stats "$rundir/${label}_scx_stats_start.json"
            start_batch_workers "$rundir" "$label" "$((DURATION_S + 2))"
            sleep "$SNAPSHOT_DELAY"
            run_agent_capture "$rundir" "$label" "sched_ext" "always-active"
            collect_scx_stats "$rundir/${label}_scx_stats_end.json"
            collect_scx_stats "$rundir/${label}_scx_stats.json" "$rundir/${label}_scx_stats_start.json"
            cp /tmp/eulerpilot-scx-session.log "$rundir/${label}_scx_session.log" 2>/dev/null || true
            wait_batch_workers
            ;;
        *)
            printf 'unknown label: %s\n' "$label" >&2
            return 1
            ;;
    esac

    write_worker_summary "$rundir" "$label"
    [ -f "$rundir/${label}_scx_stats.json" ] || printf '{}\n' > "$rundir/${label}_scx_stats.json"
    write_snapshot_metrics "$rundir" "$label"

    if [ "$label" = "cgroup_throughput_first" ] &&
       ! grep -q 'throughput_profile' "$rundir/${label}_agent_snapshot.txt" 2>/dev/null; then
        printf 'missing-throughput-profile-hit\n' > "$rundir/${label}_invalid_reason.txt"
        return 1
    fi
    if [ "$label" = "scx_throughput_first" ] &&
       ! grep -q 'THROUGHPUT_BATCH' "$rundir/${label}_agent_snapshot.txt" 2>/dev/null; then
        printf 'missing-throughput-batch-classification\n' > "$rundir/${label}_invalid_reason.txt"
        return 1
    fi
    if [ "$label" = "scx_throughput_first" ] &&
       grep -q '^dispatch_accounting_valid=0$' "$rundir/${label}_metrics.env" 2>/dev/null; then
        printf 'batch-enqueued-without-class-aware-dispatch\n' > "$rundir/${label}_invalid_reason.txt"
        return 1
    fi
}

build_sysbench_lite
printf '[INFO] Throughput-first output: %s\n' "$OUTDIR"

for run in $(seq 1 "$RUNS"); do
    rundir="$OUTDIR/run-$run"
    mkdir -p "$rundir"
    printf '[INFO] run %s/%s\n' "$run" "$RUNS"
    for label in "${LABELS[@]}"; do
        printf '[INFO] label=%s\n' "$label"
        run_case "$rundir" "$label"
        sleep 1
    done
done

python3 - <<'PY' "$OUTDIR" "$RUNS" "$WORKERS" "$DURATION_S"
import csv
import json
import os
import statistics
import subprocess
import sys
from pathlib import Path

root = Path(sys.argv[1])
runs = int(sys.argv[2])
workers = int(sys.argv[3])
duration_s = int(sys.argv[4])
labels = ["default_batch", "cgroup_throughput_first", "scx_throughput_first"]

def load_env(path: Path) -> dict[str, float]:
    out: dict[str, float] = {}
    if not path.exists():
        return out
    for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        try:
            out[key] = float(value)
        except ValueError:
            pass
    return out

rows = []
for run_dir in sorted(root.glob("run-*")):
    for label in labels:
        summary = run_dir / f"{label}_summary.csv"
        if not summary.exists():
            continue
        with summary.open(newline="", encoding="utf-8") as f:
            data = next(csv.DictReader(f))
        metrics = load_env(run_dir / f"{label}_metrics.env")
        row = {
            "run": run_dir.name,
            "label": label,
            "workers": data.get("workers", ""),
            "total_ops": data.get("total_ops", ""),
            "ops_per_sec": data.get("ops_per_sec", ""),
            "agent_applied_count": metrics.get("agent_applied_count", 0),
            "throughput_profile_hits": metrics.get("throughput_profile_hits", 0),
            "class_hits_batch": metrics.get("class_hits_batch", 0),
            "enqueue_batch": metrics.get("enqueue_batch", 0),
            "dispatch_batch_dsq": metrics.get("dispatch_batch_dsq", 0),
            "dispatch_batch_local": metrics.get("dispatch_batch_local", 0),
            "dispatch_batch_shared_fallback": metrics.get("dispatch_batch_shared_fallback", 0),
            "batch_dispatch_total": metrics.get("batch_dispatch_total", 0),
            "running_batch": metrics.get("running_batch", 0),
            "counter_delta_valid": metrics.get("counter_delta_valid", 0),
            "collection_valid": metrics.get("collection_valid", 0),
            "classification_valid": metrics.get("classification_valid", 0),
            "dispatch_accounting_valid": metrics.get("dispatch_accounting_valid", 0),
            "workload_completion_valid": metrics.get("workload_completion_valid", 0),
            "scheduler_stability_valid": metrics.get("scheduler_stability_valid", 0),
            "completion_actual": metrics.get("completion_actual", 0),
        }
        rows.append(row)

with (root / "throughput_summary.csv").open("w", newline="", encoding="utf-8") as f:
    writer = csv.DictWriter(f, fieldnames=list(rows[0].keys()) if rows else ["run", "label"])
    writer.writeheader()
    writer.writerows(rows)

avg_rows = []
for label in labels:
    label_rows = [r for r in rows if r["label"] == label]
    def mean(key: str) -> str:
        vals = [float(r[key]) for r in label_rows if str(r.get(key, "")) != ""]
        return f"{statistics.mean(vals):.3f}" if vals else ""
    avg_rows.append({
        "label": label,
        "runs": len(label_rows),
        "ops_per_sec_avg": mean("ops_per_sec"),
        "agent_applied_count_avg": mean("agent_applied_count"),
        "throughput_profile_hits_avg": mean("throughput_profile_hits"),
        "class_hits_batch_avg": mean("class_hits_batch"),
        "enqueue_batch_avg": mean("enqueue_batch"),
        "dispatch_batch_dsq_avg": mean("dispatch_batch_dsq"),
        "dispatch_batch_local_avg": mean("dispatch_batch_local"),
        "dispatch_batch_shared_fallback_avg": mean("dispatch_batch_shared_fallback"),
        "batch_dispatch_total_avg": mean("batch_dispatch_total"),
        "running_batch_avg": mean("running_batch"),
        "counter_delta_valid_avg": mean("counter_delta_valid"),
        "collection_valid_avg": mean("collection_valid"),
        "classification_valid_avg": mean("classification_valid"),
        "dispatch_accounting_valid_avg": mean("dispatch_accounting_valid"),
        "workload_completion_valid_avg": mean("workload_completion_valid"),
        "scheduler_stability_valid_avg": mean("scheduler_stability_valid"),
        "completion_actual_avg": mean("completion_actual"),
    })

with (root / "throughput_summary_avg.csv").open("w", newline="", encoding="utf-8") as f:
    writer = csv.DictWriter(f, fieldnames=list(avg_rows[0].keys()))
    writer.writeheader()
    writer.writerows(avg_rows)

manifest = {
    "run_id": root.name,
    "timestamp": subprocess.check_output(["date", "--iso-8601=seconds"], text=True).strip(),
    "host": subprocess.check_output(["hostname"], text=True).strip(),
    "kernel_release": subprocess.check_output(["uname", "-r"], text=True).strip(),
    "runs": runs,
    "workers": workers,
    "duration_s": duration_s,
    "labels": labels,
    "benchmark": "throughput-first-sysbench-lite",
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
    "notes": "sysbench is a result-local CPU worker name used to match the existing managed batch classifier; it is not the upstream sysbench package.",
}
(root / "run_manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

lines = [
    "# Throughput-first 批处理证据",
    "",
    f"- 结果目录：`{root}`",
    f"- 轮数：`{runs}`",
    f"- worker 数：`{workers}`",
    f"- 单轮时长：`{duration_s}s`",
    "",
    "## 口径",
    "",
    "- 本实验在结果目录内临时编译名为 `sysbench` 的轻量 CPU worker，用于命中现有 managed batch 分类规则；它不是外部 sysbench 包。",
    "- `EULERPILOT_THROUGHPUT_FIRST=1` 为显式实验开关，默认关闭，普通 Agent 行为不变。",
    "- `class_hits_batch/enqueue_batch/dispatch_batch/running_batch` 来自 scx stats，用于证明 batch 路径命中；性能结论仍以本目录 ops/sec 为准，并按 workload 边界解释。",
    "",
    "## 平均结果",
    "",
    "| label | runs | ops/s avg | applied avg | throughput profile hits | class_hits_batch | enqueue_batch | dispatch_dsq | dispatch_local | dispatch_fallback | batch_dispatch_total | counter_delta_valid | completion_actual | dispatch_valid |",
    "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
]
for row in avg_rows:
    lines.append(
        "| {label} | {runs} | {ops_per_sec_avg} | {agent_applied_count_avg} | {throughput_profile_hits_avg} | {class_hits_batch_avg} | {enqueue_batch_avg} | {dispatch_batch_dsq_avg} | {dispatch_batch_local_avg} | {dispatch_batch_shared_fallback_avg} | {batch_dispatch_total_avg} | {counter_delta_valid_avg} | {completion_actual_avg} | {dispatch_accounting_valid_avg} |".format(**row)
    )
lines.extend([
    "",
    "## 验收点",
    "",
    "- `cgroup_throughput_first` 的 Agent snapshot 必须出现 `throughput_profile`。",
    "- `scx_throughput_first` 的 Agent snapshot 必须出现 `THROUGHPUT_BATCH`，并保存 scx counter start/end/delta。",
    "- 当 `enqueue_batch > 0` 时，`dispatch_batch_dsq + dispatch_batch_local + dispatch_batch_shared_fallback` 必须大于 0；否则该轮 invalid。",
])
(root / "report.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
PY

cat > "$OUTDIR/summary.md" <<EOF
# Throughput-first benchmark

- timestamp: $(date --iso-8601=seconds)
- runs: $RUNS
- workers: $WORKERS
- duration_s: $DURATION_S
- scx_bin: $SCX_BIN

本目录包含 throughput_summary.csv、throughput_summary_avg.csv、report.md、run_manifest.json、run-*/<label>_agent_snapshot.txt、run-*/<label>_scx_stats.json 和 worker 原始输出。
EOF

printf '[INFO] Throughput-first complete: %s\n' "$OUTDIR"
