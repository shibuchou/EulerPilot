#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="${ROOT:-$(cd "$SCRIPT_DIR/../.." && pwd)}"
STAMP="$(date +%Y%m%d-%H%M%S)"
OUTDIR="${OUTDIR:-$ROOT/results/final/agent-overhead-$STAMP}"
RUNS="${RUNS:-3}"
DURATION_S="${DURATION_S:-8}"
INTERVAL_MS="${INTERVAL_MS:-500}"
SAMPLE_INTERVAL_S="${SAMPLE_INTERVAL_S:-0.5}"
SCX_BIN="${SCX_BIN:-$(command -v scx_eulerpilot 2>/dev/null || true)}"
SCX_BIN="${SCX_BIN:-/usr/local/bin/scx_eulerpilot}"
AGENT_BIN="${AGENT_BIN:-$ROOT/build/eulerpilot-agent}"
AGENT_CONFIG="${AGENT_CONFIG:-$ROOT/configs/agent.yaml}"

LABELS=(
    "observe_only_cgroup"
    "active_cgroup"
    "active_sched_ext"
)

mkdir -p "$OUTDIR"
OUTDIR="$(cd "$OUTDIR" && pwd)"

cleanup() {
    [ -n "${AGENT_PID:-}" ] && kill "$AGENT_PID" 2>/dev/null || true
    "$ROOT/scripts/rollback.sh" >/dev/null 2>&1 || true
}
trap cleanup EXIT

sample_proc() {
    local pid="$1"
    local out="$2"
    local t_ms="$3"
    python3 - <<'PY' "$pid" "$out" "$t_ms"
from pathlib import Path
import os
import sys

pid = sys.argv[1]
out = Path(sys.argv[2])
t_ms = sys.argv[3]
stat_path = Path("/proc") / pid / "stat"
status_path = Path("/proc") / pid / "status"
if not stat_path.exists():
    raise SystemExit(1)
raw = stat_path.read_text(encoding="utf-8", errors="ignore")
tail = raw.rsplit(")", 1)[1].strip().split()
utime = int(tail[11])
stime = int(tail[12])
rss_pages = int(tail[21])
page_size = os.sysconf("SC_PAGE_SIZE")
rss_kb = rss_pages * page_size // 1024
vmrss_kb = rss_kb
if status_path.exists():
    for line in status_path.read_text(encoding="utf-8", errors="ignore").splitlines():
        if line.startswith("VmRSS:"):
            parts = line.split()
            if len(parts) >= 2:
                vmrss_kb = int(parts[1])
            break
with out.open("a", encoding="utf-8") as f:
    f.write(f"{t_ms},{utime},{stime},{utime + stime},{rss_kb},{vmrss_kb}\n")
PY
}

sample_bpf_maps() {
    local rundir="$1"
    local label="$2"
    bpftool map show pinned /sys/fs/bpf/eulerpilot/scx_eulerpilot/v1/stats \
        > "$rundir/${label}_bpf_map_stats.txt" 2>&1 || true
    bpftool map show pinned /sys/fs/bpf/eulerpilot/scx_eulerpilot/v1/class_map \
        > "$rundir/${label}_bpf_map_class_map.txt" 2>&1 || true
    bpftool map show pinned /sys/fs/bpf/eulerpilot/scx_eulerpilot/v1/gate_state_map \
        > "$rundir/${label}_bpf_map_gate_state.txt" 2>&1 || true
}

run_agent_case() {
    local rundir="$1"
    local label="$2"
    local backend="$3"
    local active="$4"
    local extra_env="$5"
    local sample_file="$rundir/${label}_samples.csv"

    "$ROOT/scripts/rollback.sh" > "$rundir/${label}_rollback_before.log" 2>&1 || true
    sleep 1
    printf 't_ms,utime_ticks,stime_ticks,total_ticks,rss_kb,vmrss_kb\n' > "$sample_file"

    if [ "$backend" = "sched_ext" ] && { [ ! -x "$SCX_BIN" ] || [ ! -d /sys/kernel/sched_ext ]; }; then
        printf 'sched_ext unavailable or scx binary missing\n' > "$rundir/${label}_skip_reason.txt"
        return 0
    fi

    local args=(--config "$AGENT_CONFIG" --backend "$backend" --interval-ms "$INTERVAL_MS" --duration-s "$DURATION_S" --warmup-cycles 0)
    if [ "$active" = "1" ]; then
        args+=(--active)
    fi

    if [ -n "$extra_env" ]; then
        env $extra_env EULERPILOT_SCX_BINARY="$SCX_BIN" "$AGENT_BIN" "${args[@]}" > "$rundir/${label}_agent_snapshot.txt" 2> "$rundir/${label}_agent.err" &
    else
        EULERPILOT_SCX_BINARY="$SCX_BIN" "$AGENT_BIN" "${args[@]}" > "$rundir/${label}_agent_snapshot.txt" 2> "$rundir/${label}_agent.err" &
    fi
    AGENT_PID=$!

    local start_ms
    start_ms="$(date +%s%3N)"
    while kill -0 "$AGENT_PID" 2>/dev/null; do
        local now_ms
        now_ms="$(date +%s%3N)"
        sample_proc "$AGENT_PID" "$sample_file" "$((now_ms - start_ms))" 2>/dev/null || true
        sample_bpf_maps "$rundir" "$label"
        sleep "$SAMPLE_INTERVAL_S"
    done
    wait "$AGENT_PID"
    unset AGENT_PID
    "$ROOT/scripts/rollback.sh" > "$rundir/${label}_rollback_after.log" 2>&1 || true
}

printf '[INFO] Agent overhead output: %s\n' "$OUTDIR"
for run in $(seq 1 "$RUNS"); do
    rundir="$OUTDIR/run-$run"
    mkdir -p "$rundir"
    printf '[INFO] run %s/%s\n' "$run" "$RUNS"
    run_agent_case "$rundir" "observe_only_cgroup" "cgroup_v2" "0" ""
    run_agent_case "$rundir" "active_cgroup" "cgroup_v2" "1" ""
    run_agent_case "$rundir" "active_sched_ext" "sched_ext" "1" "EULERPILOT_GATE_MODE=normal"
done

python3 - <<'PY' "$OUTDIR" "$RUNS" "$DURATION_S"
import csv
import json
import os
import statistics
import subprocess
import sys
from pathlib import Path

root = Path(sys.argv[1])
runs = int(sys.argv[2])
duration_s = float(sys.argv[3])
labels = ["observe_only_cgroup", "active_cgroup", "active_sched_ext"]
hz = os.sysconf(os.sysconf_names["SC_CLK_TCK"])

rows = []
for run_dir in sorted(root.glob("run-*")):
    for label in labels:
        sample_path = run_dir / f"{label}_samples.csv"
        skip_path = run_dir / f"{label}_skip_reason.txt"
        if skip_path.exists():
            rows.append({
                "run": run_dir.name,
                "label": label,
                "status": "skipped",
                "samples": 0,
                "cpu_seconds": "",
                "cpu_percent_of_one_core": "",
                "rss_kb_avg": "",
                "rss_kb_max": "",
                "bpf_map_snapshot": "0",
            })
            continue
        if not sample_path.exists():
            continue
        with sample_path.open(newline="", encoding="utf-8") as f:
            samples = list(csv.DictReader(f))
        if len(samples) < 2:
            continue
        ticks = [int(row["total_ticks"]) for row in samples]
        rss = [int(row["vmrss_kb"]) for row in samples]
        cpu_seconds = (ticks[-1] - ticks[0]) / hz
        cpu_pct = cpu_seconds / duration_s * 100.0 if duration_s else 0.0
        rows.append({
            "run": run_dir.name,
            "label": label,
            "status": "present",
            "samples": len(samples),
            "cpu_seconds": f"{cpu_seconds:.6f}",
            "cpu_percent_of_one_core": f"{cpu_pct:.3f}",
            "rss_kb_avg": f"{statistics.mean(rss):.3f}",
            "rss_kb_max": max(rss),
            "bpf_map_snapshot": "1" if (run_dir / f"{label}_bpf_map_stats.txt").exists() else "0",
        })

with (root / "agent_overhead_summary.csv").open("w", newline="", encoding="utf-8") as f:
    writer = csv.DictWriter(f, fieldnames=list(rows[0].keys()) if rows else ["run", "label"])
    writer.writeheader()
    writer.writerows(rows)

avg_rows = []
for label in labels:
    label_rows = [r for r in rows if r["label"] == label and r["status"] == "present"]
    def mean(key: str) -> str:
        vals = [float(r[key]) for r in label_rows if str(r.get(key, "")) != ""]
        return f"{statistics.mean(vals):.3f}" if vals else ""
    avg_rows.append({
        "label": label,
        "runs_present": len(label_rows),
        "cpu_seconds_avg": mean("cpu_seconds"),
        "cpu_percent_of_one_core_avg": mean("cpu_percent_of_one_core"),
        "rss_kb_avg": mean("rss_kb_avg"),
        "rss_kb_max": max([int(r["rss_kb_max"]) for r in label_rows], default=""),
        "skipped": sum(1 for r in rows if r["label"] == label and r["status"] == "skipped"),
    })

with (root / "agent_overhead_summary_avg.csv").open("w", newline="", encoding="utf-8") as f:
    writer = csv.DictWriter(f, fieldnames=list(avg_rows[0].keys()))
    writer.writeheader()
    writer.writerows(avg_rows)

manifest = {
    "run_id": root.name,
    "timestamp": subprocess.check_output(["date", "--iso-8601=seconds"], text=True).strip(),
    "host": subprocess.check_output(["hostname"], text=True).strip(),
    "kernel_release": subprocess.check_output(["uname", "-r"], text=True).strip(),
    "runs": runs,
    "duration_s": duration_s,
    "benchmark": "agent-overhead",
    "notes": "CPU percent is computed from /proc/<pid>/stat ticks over the configured agent duration; it is control-plane overhead evidence, not workload CPU accounting.",
}
(root / "run_manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

lines = [
    "# Agent 自身开销证据",
    "",
    f"- 结果目录：`{root}`",
    f"- 轮数：`{runs}`",
    f"- 单轮 Agent 时长：`{duration_s}s`",
    "",
    "## 指标口径",
    "",
    "- CPU 开销来自 `/proc/<pid>/stat` 的 utime/stime ticks，换算为单核百分比。",
    "- RSS 来自 `/proc/<pid>/status` 的 VmRSS。",
    "- BPF map 以 `bpftool map show pinned ...` 快照保存，只作为内核对象存在性证据。",
    "",
    "## 平均结果",
    "",
    "| label | present runs | CPU seconds avg | one-core CPU % avg | RSS KB avg | RSS KB max | skipped |",
    "| --- | ---: | ---: | ---: | ---: | ---: | ---: |",
]
for row in avg_rows:
    lines.append(
        "| {label} | {runs_present} | {cpu_seconds_avg} | {cpu_percent_of_one_core_avg} | {rss_kb_avg} | {rss_kb_max} | {skipped} |".format(**row)
    )
lines.extend([
    "",
    "## 结论边界",
    "",
    "本实验用于说明 EulerPilot 用户态控制面本身的 CPU/RSS 级别开销。它不替代 Redis/Nginx 性能对照，也不声称内核调度路径没有成本。",
])
(root / "report.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
PY

cat > "$OUTDIR/summary.md" <<EOF
# Agent overhead

- timestamp: $(date --iso-8601=seconds)
- runs: $RUNS
- duration_s: $DURATION_S
- interval_ms: $INTERVAL_MS
- scx_bin: $SCX_BIN

本目录包含 agent_overhead_summary.csv、agent_overhead_summary_avg.csv、report.md、run_manifest.json、run-*/<label>_samples.csv 和 Agent snapshot/log。
EOF

printf '[INFO] Agent overhead complete: %s\n' "$OUTDIR"
