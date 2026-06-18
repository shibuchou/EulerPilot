#!/usr/bin/env python3
import csv
import sys
from pathlib import Path


def pct_change(new: float, old: float) -> str:
    if old == 0:
        return "N/A"
    return f"{((new - old) / old) * 100:.2f}%"


def load_single_row(path: Path) -> dict[str, str]:
    with path.open(newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            return row
    return {}


def collect_agent_evidence(result_dir: Path) -> list[str]:
    evidence = []
    for name in ("active_noisy_agent_snapshot.txt", "active_noisy_sched_ext_agent_snapshot.txt"):
        snapshot = result_dir / name
        if snapshot.exists():
            for line in snapshot.read_text(encoding="utf-8", errors="ignore").splitlines():
                if ("nginx" in line or "stress-ng" in line) and "applied=yes" in line:
                    evidence.append(f"- {line}")
    return evidence


def main() -> int:
    if len(sys.argv) != 5:
        print("usage: render_nginx_report.py <baseline.csv> <noisy.csv> <active.csv> <report.md>", file=sys.stderr)
        return 1

    baseline = load_single_row(Path(sys.argv[1]))
    noisy = load_single_row(Path(sys.argv[2]))
    active = load_single_row(Path(sys.argv[3]))
    report_md = Path(sys.argv[4])
    result_dir = report_md.parent

    lines = [
        "# Nginx 抗干扰实验报告",
        "",
        "## 报告来源",
        "",
        f"- 结果目录：`{result_dir}`",
        "",
        "## 汇总表",
        "",
        "| 阶段 | Requests/sec | Avg Latency | P99 Latency |",
        "| --- | ---: | ---: | ---: |",
        f"| baseline | {baseline.get('requests_per_sec','')} | {baseline.get('avg_latency','')} | {baseline.get('p99_latency','')} |",
        f"| default_noisy | {noisy.get('requests_per_sec','')} | {noisy.get('avg_latency','')} | {noisy.get('p99_latency','')} |",
        f"| active_noisy | {active.get('requests_per_sec','')} | {active.get('avg_latency','')} | {active.get('p99_latency','')} |",
        "",
    ]

    if noisy and active:
        rps_change = pct_change(float(active["requests_per_sec"]), float(noisy["requests_per_sec"]))
        lines.extend([
            "## 自动结论",
            "",
            f"- 相对 `default_noisy`，`active_noisy` 的吞吐变化为 `{rps_change}`。",
            f"- `default_noisy` 的 P99 为 `{noisy.get('p99_latency','')}`，`active_noisy` 的 P99 为 `{active.get('p99_latency','')}`。",
            "",
        ])

    evidence = collect_agent_evidence(result_dir)
    lines.extend(["## Agent 证据摘录", ""])
    if evidence:
        lines.extend(evidence)
    else:
        lines.append("- 当前未提取到 Nginx / stress-ng 的 `applied=yes` 关键证据。")

    report_md.write_text("\n".join(lines), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
