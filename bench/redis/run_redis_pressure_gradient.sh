#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="${ROOT:-$(cd "$SCRIPT_DIR/../.." && pwd)}"
STAMP="$(date +%Y%m%d-%H%M%S)"
OUTDIR="${OUTDIR:-$ROOT/results/final/redis-pressure-gradient-$STAMP}"
RUNS="${RUNS:-3}"
WORKERS="${WORKERS:-0 1 2 4 8}"
LABEL_FILTER="${LABEL_FILTER:-quiet_default,noisy_default,noisy_cgroup_v2,noisy_scx_psi}"

mkdir -p "$OUTDIR"

printf '[INFO] Redis pressure gradient output: %s\n' "$OUTDIR"
for workers in $WORKERS; do
    case_dir="$OUTDIR/workers-$workers"
    printf '[INFO] pressure workers=%s runs=%s labels=%s\n' "$workers" "$RUNS" "$LABEL_FILTER"
    OUTDIR="$case_dir" RUNS="$RUNS" STRESS_WORKERS="$workers" LABEL_FILTER="$LABEL_FILTER" \
        bash "$ROOT/bench/redis/run_redis_sched_ext_compare.sh"
done

python3 - <<'PY' "$OUTDIR" "$OUTDIR/pressure_gradient_summary.csv" "$OUTDIR/report.md"
import csv
import sys
from pathlib import Path

root = Path(sys.argv[1])
summary_csv = Path(sys.argv[2])
report_md = Path(sys.argv[3])

rows = []
for case_dir in sorted(root.glob("workers-*"), key=lambda p: int(p.name.split("-", 1)[1])):
    workers = int(case_dir.name.split("-", 1)[1])
    compare = case_dir / "compare_summary_avg.csv"
    if not compare.exists():
        rows.append({"workers": workers, "status": "missing_compare"})
        continue
    with compare.open(newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            out = {"workers": workers, "status": "present", "test": row.get("test", "")}
            for key, value in row.items():
                if key == "test":
                    continue
                if key.endswith("_rps_avg") or key.endswith("_p99_ms_avg") or key.endswith("_cpu_per_10k_requests_avg"):
                    out[key] = value
            rows.append(out)

fieldnames = []
for row in rows:
    for key in row:
        if key not in fieldnames:
            fieldnames.append(key)

with summary_csv.open("w", newline="", encoding="utf-8") as f:
    writer = csv.DictWriter(f, fieldnames=fieldnames)
    writer.writeheader()
    writer.writerows(rows)

lines = [
    "# Redis 压力递增梯度实验",
    "",
    f"- 结果目录：`{root}`",
    "- worker 档位：0 / 1 / 2 / 4 / 8",
    "- 组别：quiet_default / noisy_default / noisy_cgroup_v2 / noisy_scx_psi",
    "",
    "## 汇总说明",
    "",
    "本实验观察干扰增强时 default、cgroup v2 与 sched_ext psi 的收益和代价变化。结论只限定在 Redis latency-sensitive 混布场景，不推广为所有 workload 的绝对提升。",
    "",
    "## GET 视角核心表",
    "",
]
get_rows = [row for row in rows if row.get("test") == "GET"]
if get_rows:
    headers = [
        "workers",
        "noisy_default_rps_avg",
        "noisy_cgroup_v2_rps_avg",
        "noisy_scx_psi_rps_avg",
        "noisy_default_p99_ms_avg",
        "noisy_cgroup_v2_p99_ms_avg",
        "noisy_scx_psi_p99_ms_avg",
        "noisy_default_cpu_per_10k_requests_avg",
        "noisy_cgroup_v2_cpu_per_10k_requests_avg",
        "noisy_scx_psi_cpu_per_10k_requests_avg",
    ]
    lines.append("| " + " | ".join(headers) + " |")
    lines.append("| " + " | ".join(["---"] + ["---:" for _ in headers[1:]]) + " |")
    for row in get_rows:
        lines.append("| " + " | ".join(str(row.get(h, "")) for h in headers) + " |")
else:
    lines.append("- 未找到 GET 汇总行，请检查子目录 compare_summary_avg.csv。")

lines.extend([
    "",
    "## 文件",
    "",
    "- `pressure_gradient_summary.csv`：所有 test 的机器可读汇总。",
    "- `workers-*/`：每个压力档位的完整 Redis compare 子结果。",
])
report_md.write_text("\n".join(lines) + "\n", encoding="utf-8")
PY

printf '[INFO] Redis pressure gradient complete: %s\n' "$OUTDIR"
