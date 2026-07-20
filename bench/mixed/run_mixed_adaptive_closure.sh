#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="${ROOT:-$(cd "$SCRIPT_DIR/../.." && pwd)}"
STAMP="$(date +%Y%m%d-%H%M%S)"
OUTDIR="${OUTDIR:-$ROOT/results/final/mixed-adaptive-$STAMP}"
RUNS="${RUNS:-3}"
REDIS_PORT="${REDIS_PORT:-6391}"
BENCH_CLIENTS="${BENCH_CLIENTS:-16}"
BENCH_REQUESTS="${BENCH_REQUESTS:-20000}"
STRESS_WORKERS="${STRESS_WORKERS:-2}"
INTERVAL_MS="${INTERVAL_MS:-500}"
DURATION_S="${DURATION_S:-8}"
PSI_PROBE_CLIENTS="${PSI_PROBE_CLIENTS:-64}"
PSI_PROBE_REQUESTS="${PSI_PROBE_REQUESTS:-30000}"
PSI_PROBE_TESTS="${PSI_PROBE_TESTS:-set,get,incr}"
SCX_BIN="${SCX_BIN:-$(command -v scx_eulerpilot 2>/dev/null || true)}"
SCX_BIN="${SCX_BIN:-/usr/local/bin/scx_eulerpilot}"

mkdir -p "$OUTDIR"
OUTDIR="$(cd "$OUTDIR" && pwd)"

cleanup() {
    [ -n "${STRESS_PID:-}" ] && kill "$STRESS_PID" 2>/dev/null || true
    [ -n "${PSI_PROBE_PID:-}" ] && kill "$PSI_PROBE_PID" 2>/dev/null || true
    [ -n "${REDIS_PID:-}" ] && kill "$REDIS_PID" 2>/dev/null || true
    pkill -f 'redis-benchmark' 2>/dev/null || true
    "$ROOT/scripts/rollback.sh" >/dev/null 2>&1 || true
}
trap cleanup EXIT

wait_for_redis() {
    for _ in $(seq 1 30); do
        redis-cli -h 127.0.0.1 -p "$REDIS_PORT" ping >/dev/null 2>&1 && return 0
        sleep 0.2
    done
    sed 's/^/[redis] /' "$OUTDIR/redis.log" >&2 2>/dev/null || true
    return 1
}

start_stress() {
    local rundir="$1"
    local phase="$2"
    if [ "$STRESS_WORKERS" -le 0 ]; then
        printf 'stress_skipped_workers=0\n' > "$rundir/${phase}_stress.log"
        return 0
    fi
    stress-ng --cpu "$STRESS_WORKERS" --timeout "$((DURATION_S + 4))s" \
        > "$rundir/${phase}_stress.log" 2>&1 &
    STRESS_PID=$!
    sleep 1
}

stop_stress() {
    [ -n "${STRESS_PID:-}" ] && kill "$STRESS_PID" 2>/dev/null || true
    [ -n "${STRESS_PID:-}" ] && wait "$STRESS_PID" 2>/dev/null || true
    unset STRESS_PID
}

start_psi_redis_probe() {
    local rundir="$1"
    local phase="$2"
    (
        trap 'kill "${child:-0}" 2>/dev/null || true; exit 0' TERM INT
        while :; do
            redis-benchmark -h 127.0.0.1 -p "$REDIS_PORT" \
                -t "$PSI_PROBE_TESTS" \
                -c "$PSI_PROBE_CLIENTS" \
                -n "$PSI_PROBE_REQUESTS" \
                --csv >> "$rundir/${phase}_psi_probe.csv" \
                2>> "$rundir/${phase}_psi_probe.err" &
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

run_redis_phase() {
    local rundir="$1"
    local phase="$2"
    rm -f /tmp/eulerpilot-psi-gate-trace.jsonl /tmp/eulerpilot-scx-session.log /tmp/eulerpilot-scx.log
    redis-benchmark -h 127.0.0.1 -p "$REDIS_PORT" -c "$BENCH_CLIENTS" -n "$BENCH_REQUESTS" \
        --csv > "$rundir/${phase}_redis_benchmark.csv" &
    local bench_pid=$!

    EULERPILOT_GATE_MODE=psi \
    EULERPILOT_SCX_BINARY="$SCX_BIN" \
    EULERPILOT_CPU_PSI_THRESHOLD="${EULERPILOT_CPU_PSI_THRESHOLD:-0.0}" \
    EULERPILOT_LATENCY_WAIT_THRESHOLD_NS="${EULERPILOT_LATENCY_WAIT_THRESHOLD_NS:-1}" \
    EULERPILOT_BACKGROUND_RUNTIME_THRESHOLD_NS="${EULERPILOT_BACKGROUND_RUNTIME_THRESHOLD_NS:-1}" \
    EULERPILOT_GATE_ACTIVATION_WINDOWS="${EULERPILOT_GATE_ACTIVATION_WINDOWS:-1}" \
        "$ROOT/scripts/capture_agent_snapshot.sh" \
        "$rundir/${phase}_agent_snapshot.txt" \
        "$INTERVAL_MS" --active "$DURATION_S" 0 sched_ext

    wait "$bench_pid"
    awk -f "$ROOT/scripts/extract_redis_metrics.awk" \
        "$rundir/${phase}_redis_benchmark.csv" > "$rundir/${phase}_summary.csv"
    "$SCX_BIN" --gate-status > "$rundir/${phase}_gate_status.txt" 2>&1 || true
    python3 "$ROOT/scripts/collect_scx_stats.py" "$rundir/${phase}_scx_stats.json" 2>/dev/null || true
    cp /tmp/eulerpilot-psi-gate-trace.jsonl "$rundir/${phase}_psi_gate_trace.jsonl" 2>/dev/null || true
    cp /tmp/eulerpilot-scx-session.log "$rundir/${phase}_scx_session.log" 2>/dev/null || true
}

run_case() {
    local rundir="$1"
    "$ROOT/scripts/rollback.sh" > "$rundir/rollback_before.log" 2>&1 || true
    rm -f /tmp/eulerpilot-psi-gate-trace.jsonl /tmp/eulerpilot-scx-session.log /tmp/eulerpilot-scx.log
    sleep 1

    run_redis_phase "$rundir" "quiet_pre"

    start_stress "$rundir" "pressure_active"
    start_psi_redis_probe "$rundir" "pressure_active"
    sleep 0.5
    run_redis_phase "$rundir" "pressure_active"
    stop_psi_redis_probe
    stop_stress

    sleep 2
    run_redis_phase "$rundir" "recovery"
    cat "$rundir/"*_psi_gate_trace.jsonl > "$rundir/combined_psi_gate_trace.jsonl" 2>/dev/null || true
    "$ROOT/scripts/rollback.sh" > "$rundir/rollback_after.log" 2>&1 || true

    if ! grep -q '"next_state":"ACTIVE"' "$rundir/pressure_active_psi_gate_trace.jsonl" 2>/dev/null; then
        printf 'missing-active-transition\n' > "$rundir/invalid_reason.txt"
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

redis-server "$OUTDIR/redis.conf" >/dev/null 2>&1 &
REDIS_PID=$!
wait_for_redis

printf '[INFO] Mixed adaptive output: %s\n' "$OUTDIR"
for run in $(seq 1 "$RUNS"); do
    rundir="$OUTDIR/run-$run"
    mkdir -p "$rundir"
    printf '[INFO] run %s/%s\n' "$run" "$RUNS"
    run_case "$rundir"
done

python3 - <<'PY' "$OUTDIR" "$RUNS" "$STRESS_WORKERS"
import csv
import json
import statistics
import subprocess
import sys
from pathlib import Path

root = Path(sys.argv[1])
runs = int(sys.argv[2])
stress_workers = int(sys.argv[3])
phases = ["quiet_pre", "pressure_active", "recovery"]

def get_metric(summary: Path, test_name: str, key: str) -> str:
    if not summary.exists():
        return ""
    with summary.open(newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            if row.get("test") == test_name:
                return row.get(key, "")
    return ""

def parse_trace(path: Path) -> dict[str, str]:
    out = {
        "active_seen": "0",
        "cooldown_seen": "0",
        "switch_latency_ms": "",
        "recovery_seen": "0",
        "events": "0",
    }
    if not path.exists():
        return out
    events = []
    for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
        try:
            events.append(json.loads(line))
        except json.JSONDecodeError:
            continue
    out["events"] = str(len(events))
    if not events:
        return out
    first_ts = events[0].get("timestamp_ns", 0)
    for event in events:
        if event.get("next_state") == "ACTIVE" and out["active_seen"] == "0":
            out["active_seen"] = "1"
            out["switch_latency_ms"] = f"{(int(event.get('timestamp_ns', first_ts)) - int(first_ts)) / 1_000_000:.3f}"
        if event.get("next_state") == "COOLDOWN":
            out["cooldown_seen"] = "1"
        if event.get("background_workload_present") is False:
            out["recovery_seen"] = "1"
    return out

def load_json(path: Path) -> dict:
    if not path.exists():
        return {}
    try:
        return json.loads(path.read_text(encoding="utf-8", errors="ignore"))
    except json.JSONDecodeError:
        return {}

rows = []
for run_dir in sorted(root.glob("run-*")):
    for phase in phases:
        trace = parse_trace(run_dir / f"{phase}_psi_gate_trace.jsonl")
        snapshot_path = run_dir / f"{phase}_agent_snapshot.txt"
        snapshot_text = snapshot_path.read_text(encoding="utf-8", errors="ignore") if snapshot_path.exists() else ""
        scx_stats = load_json(run_dir / f"{phase}_scx_stats.json")
        scheduler_update = (
            "scx-class-map-updated" in snapshot_text
            or "gate-state-map-updated" in snapshot_text
            or int(scx_stats.get("class_map_hit", 0)) > 0
        )
        rows.append({
            "run": run_dir.name,
            "phase": phase,
            "get_rps": get_metric(run_dir / f"{phase}_summary.csv", "GET", "rps"),
            "get_p99_ms": get_metric(run_dir / f"{phase}_summary.csv", "GET", "p99_latency_ms"),
            "active_seen": trace["active_seen"],
            "cooldown_seen": trace["cooldown_seen"],
            "recovery_seen": trace["recovery_seen"],
            "switch_latency_ms": trace["switch_latency_ms"],
            "trace_events": trace["events"],
            "scheduler_update_evidence": "1" if scheduler_update else "0",
        })

with (root / "mixed_adaptive_timeline.csv").open("w", newline="", encoding="utf-8") as f:
    writer = csv.DictWriter(f, fieldnames=list(rows[0].keys()) if rows else ["run", "phase"])
    writer.writeheader()
    writer.writerows(rows)

avg_rows = []
for phase in phases:
    phase_rows = [r for r in rows if r["phase"] == phase]
    def mean(key: str) -> str:
        vals = [float(r[key]) for r in phase_rows if str(r.get(key, "")) != ""]
        return f"{statistics.mean(vals):.3f}" if vals else ""
    avg_rows.append({
        "phase": phase,
        "runs": len(phase_rows),
        "get_rps_avg": mean("get_rps"),
        "get_p99_ms_avg": mean("get_p99_ms"),
        "active_seen_count": sum(1 for r in phase_rows if r["active_seen"] == "1"),
        "cooldown_seen_count": sum(1 for r in phase_rows if r["cooldown_seen"] == "1"),
        "recovery_seen_count": sum(1 for r in phase_rows if r["recovery_seen"] == "1"),
        "switch_latency_ms_avg": mean("switch_latency_ms"),
        "scheduler_update_evidence_count": sum(1 for r in phase_rows if r["scheduler_update_evidence"] == "1"),
    })

with (root / "mixed_adaptive_summary.csv").open("w", newline="", encoding="utf-8") as f:
    writer = csv.DictWriter(f, fieldnames=list(avg_rows[0].keys()))
    writer.writeheader()
    writer.writerows(avg_rows)

manifest = {
    "run_id": root.name,
    "timestamp": subprocess.check_output(["date", "--iso-8601=seconds"], text=True).strip(),
    "host": subprocess.check_output(["hostname"], text=True).strip(),
    "kernel_release": subprocess.check_output(["uname", "-r"], text=True).strip(),
    "runs": runs,
    "stress_workers": stress_workers,
    "benchmark": "mixed-adaptive-redis-psi",
    "notes": "pressure_active uses a Redis PSI probe only to trigger PSI gate; it is activation evidence, not net performance comparison.",
}
(root / "run_manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

lines = [
    "# Mixed-Adaptive 完整闭环证据",
    "",
    f"- 结果目录：`{root}`",
    f"- 轮数：`{runs}`",
    f"- stress workers：`{stress_workers}`",
    "",
    "## 链路",
    "",
    "压力出现 -> PSI/wait 变化 -> Gate 进入 ACTIVE -> class_map / gate_state 更新 -> 压力消失 -> recovery 阶段确认恢复与 rollback。",
    "",
    "## 平均结果",
    "",
    "| phase | runs | GET RPS avg | GET P99 ms avg | ACTIVE 次数 | COOLDOWN 次数 | recovery evidence | switch latency ms | scheduler update evidence |",
    "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
]
for row in avg_rows:
    lines.append(
        "| {phase} | {runs} | {get_rps_avg} | {get_p99_ms_avg} | {active_seen_count} | {cooldown_seen_count} | {recovery_seen_count} | {switch_latency_ms_avg} | {scheduler_update_evidence_count} |".format(**row)
    )
lines.extend([
    "",
    "## 结论边界",
    "",
    "`pressure_active` 阶段包含额外 Redis PSI probe，目的在于稳定触发 PSI gate 和调度路径，不作为净性能提升结论；性能收益仍需结合无额外 probe 的 Redis/Nginx RUNS=5 对照解释。",
])
(root / "report.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
PY

cat > "$OUTDIR/summary.md" <<EOF
# Mixed-Adaptive closure

- timestamp: $(date --iso-8601=seconds)
- runs: $RUNS
- redis port: $REDIS_PORT
- bench clients: $BENCH_CLIENTS
- bench requests: $BENCH_REQUESTS
- stress workers: $STRESS_WORKERS
- scx bin: $SCX_BIN

本目录包含 mixed_adaptive_timeline.csv、mixed_adaptive_summary.csv、report.md、run_manifest.json、run-*/<phase>_psi_gate_trace.jsonl、run-*/<phase>_agent_snapshot.txt、run-*/<phase>_gate_status.txt 和 rollback 日志。
EOF

printf '[INFO] Mixed adaptive complete: %s\n' "$OUTDIR"
