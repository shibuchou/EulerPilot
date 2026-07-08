#!/usr/bin/env python3
import csv
import json
import sys
from pathlib import Path


def pct_change(new: float, old: float) -> str:
    if old == 0:
        return "N/A"
    return f"{((new - old) / old) * 100:.2f}%"


def read_text(path: Path) -> str:
    if not path.exists():
        return ""
    return path.read_text(encoding="utf-8", errors="ignore").strip()


def main() -> int:
    if len(sys.argv) != 4:
        print("usage: render_redis_backend_compare_report.py <summary.csv> <run_manifest.json> <report.md>", file=sys.stderr)
        return 1

    summary_csv = Path(sys.argv[1])
    manifest_json = Path(sys.argv[2])
    report_md = Path(sys.argv[3])

    manifest = json.loads(manifest_json.read_text(encoding="utf-8"))
    labels = manifest["labels"]
    label_titles = manifest.get("label_titles", {})

    rows = []
    with summary_csv.open(newline="", encoding="utf-8") as f:
        rows = list(csv.DictReader(f))

    lines = [
        "# Redis sched_ext 后端正式对照报告",
        "",
        "## 运行信息",
        "",
        f"- 结果目录：`{report_md.parent}`",
        f"- 主机：`{manifest.get('host', '')}`",
        f"- 内核：`{manifest.get('kernel_release', '')}`",
        f"- 轮数：`{manifest.get('runs', '')}`",
        f"- Redis 端口：`{manifest.get('redis_port', '')}`",
        f"- bench clients：`{manifest.get('bench_clients', '')}`",
        f"- bench requests：`{manifest.get('bench_requests', '')}`",
        f"- stress workers：`{manifest.get('stress_workers', '')}`",
        f"- sched_ext switch mode：`{manifest.get('sched_ext_switch_mode', 'full')}`",
        "",
        "## 组别说明",
        "",
    ]

    for label in labels:
        title = label_titles.get(label, label)
        lines.append(f"- `{label}`：{title}")

    lines.extend(
        [
            "",
            "## 汇总表",
            "",
        ]
    )

    header = ["测试项"]
    for label in labels:
        header.append(f"{label} RPS")
        header.append(f"{label} P99(ms)")
        header.append(f"{label} CPU/10k")
    lines.append("| " + " | ".join(header) + " |")
    lines.append("| " + " | ".join(["---"] + ["---:" for _ in range(len(header) - 1)]) + " |")

    for row in rows:
        cols = [row["test"]]
        for label in labels:
            cols.append(row.get(f"{label}_rps_avg", ""))
            cols.append(row.get(f"{label}_p99_ms_avg", ""))
            cols.append(row.get(f"{label}_cpu_per_10k_requests_avg", ""))
        lines.append("| " + " | ".join(cols) + " |")

    if "noisy_default" in labels:
        lines.extend(["", "## 相对 `noisy_default` 的自动观察", ""])
        for row in rows:
            default_rps = float(row["noisy_default_rps_avg"])
            default_p99 = float(row["noisy_default_p99_ms_avg"])
            lines.append(f"### {row['test']}")
            for label in labels:
                if label == "noisy_default":
                    continue
                rps = float(row[f"{label}_rps_avg"])
                p99 = float(row[f"{label}_p99_ms_avg"])
                lines.append(
                    f"- `{label}`：RPS {pct_change(rps, default_rps)}，P99 {pct_change(p99, default_p99)}"
                )

    lines.extend(["", "## 运行边界", ""])
    lines.extend(
        [
            "- `redis-server` 为被保护服务，`stress-ng` 为后台干扰任务。",
            "- `redis-benchmark` 固定留在默认根组，不参与分类控制。",
            "- `cgroup_v2` 组运行前必须保证 `sched_ext state=disabled`。",
            "- `sched_ext` 组运行时必须保证 `sched_ext state=enabled`，并保留 gate 状态与统计快照。",
        ]
    )

    lines.extend(["", "## 当前结论", ""])
    lines.append("- 本报告用于正式后端对照阶段的第一轮汇总，后续还需要继续扩大 `RUNS` 并补图表。")

    report_md.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
