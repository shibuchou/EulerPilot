#!/usr/bin/env python3
import csv
import json
import sys
from pathlib import Path


def pct_change(new: float, old: float) -> str:
    if old == 0:
        return "N/A"
    return f"{((new - old) / old) * 100:.2f}%"


def main() -> int:
    if len(sys.argv) != 4:
        print("usage: render_nginx_backend_compare_report.py <summary.csv> <manifest.json> <report.md>", file=sys.stderr)
        return 1

    summary_csv = Path(sys.argv[1])
    manifest_json = Path(sys.argv[2])
    report_md = Path(sys.argv[3])

    manifest = json.loads(manifest_json.read_text(encoding="utf-8"))
    labels = manifest["labels"]
    label_titles = manifest.get("label_titles", {})

    with summary_csv.open(newline="", encoding="utf-8") as f:
        row = next(csv.DictReader(f))

    lines = [
        "# Nginx sched_ext 后端正式对照报告",
        "",
        "## 运行信息",
        "",
        f"- 结果目录：`{report_md.parent}`",
        f"- 主机：`{manifest.get('host', '')}`",
        f"- 内核：`{manifest.get('kernel_release', '')}`",
        f"- 轮数：`{manifest.get('runs', '')}`",
        f"- Nginx 端口：`{manifest.get('nginx_port', '')}`",
        f"- wrk threads：`{manifest.get('wrk_threads', '')}`",
        f"- wrk connections：`{manifest.get('wrk_connections', '')}`",
        f"- wrk duration：`{manifest.get('wrk_duration', '')}`",
        f"- stress workers：`{manifest.get('stress_workers', '')}`",
        "",
        "## 组别说明",
        "",
    ]

    for label in labels:
        lines.append(f"- `{label}`：{label_titles.get(label, label)}")

    lines.extend(["", "## 汇总表", ""])
    lines.append("| 组别 | Requests/sec | P99(ms) | CPU/10k requests |")
    lines.append("| --- | ---: | ---: | ---: |")
    for label in labels:
        lines.append(
            f"| {label} | {row.get(f'{label}_rps_avg','')} | {row.get(f'{label}_p99_ms_avg','')} | "
            f"{row.get(f'{label}_cpu_per_10k_requests_avg','')} |"
        )

    if "noisy_default" in labels:
        noisy_rps = float(row["noisy_default_rps_avg"])
        noisy_p99 = float(row["noisy_default_p99_ms_avg"])
        lines.extend(["", "## 相对 `noisy_default` 的自动观察", ""])
        for label in labels:
            if label == "noisy_default":
                continue
            lines.append(
                f"- `{label}`：RPS {pct_change(float(row[f'{label}_rps_avg']), noisy_rps)}，"
                f"P99 {pct_change(float(row[f'{label}_p99_ms_avg']), noisy_p99)}"
            )

    lines.extend(["", "## 当前结论", ""])
    lines.append("- 本报告用于 Nginx sched_ext 正式对照的第一轮汇总，后续还需要扩大 `RUNS` 并补图表。")

    report_md.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
