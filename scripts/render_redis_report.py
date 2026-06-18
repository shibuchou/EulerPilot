#!/usr/bin/env python3
import csv
import sys
from pathlib import Path


def pct_change(new: float, old: float) -> str:
    if old == 0:
        return "N/A"
    return f"{((new - old) / old) * 100:.2f}%"


def collect_agent_evidence(result_dir: Path) -> list[str]:
    evidence = []
    candidate_paths: list[Path] = []
    for run_dir in sorted(result_dir.glob("run-*"))[:2]:
        for name in ("active_noisy_agent_snapshot.txt", "active_noisy_sched_ext_agent_snapshot.txt"):
            path = run_dir / name
            if path.exists():
                candidate_paths.append(path)
    for path in candidate_paths:
        text = path.read_text(encoding="utf-8", errors="ignore").splitlines()
        for line in text:
            if ("redis-server" in line or "stress-ng" in line) and "applied=yes" in line:
                evidence.append(f"- `{path.parent.name}`: {line}")
    return evidence


def read_psi_summary(result_dir: Path) -> list[str]:
    lines = []
    for phase in ("pre", "post"):
        psi_file = result_dir / phase / "pressure_cpu.txt"
        if psi_file.exists():
            content = psi_file.read_text(encoding="utf-8", errors="ignore").strip().splitlines()
            for line in content:
                lines.append(f"- `{phase}`: {line}")
    return lines


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: render_redis_report.py <compare_summary_avg.csv> <report.md>", file=sys.stderr)
        return 1

    compare_csv = Path(sys.argv[1]).resolve()
    report_md = Path(sys.argv[2]).resolve()
    result_dir = report_md.parent

    rows = []
    with compare_csv.open(newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        rows = list(reader)

    lines = [
        "# Redis 抗干扰实验报告",
        "",
        "## 报告来源",
        "",
        f"- 结果目录：`{result_dir}`",
        f"- 对比汇总文件：`{compare_csv}`",
        "",
        "## 结果概览",
        "",
        "本报告基于多轮 `baseline/default_noisy/active_noisy` 对比结果自动生成。",
        "",
        "## 汇总表",
        "",
        "| 测试项 | default noisy RPS均值 | active noisy RPS均值 | RPS变化 | default noisy P99均值(ms) | active noisy P99均值(ms) | P99变化 | RPS标准差(active) | P99标准差(active) |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]

    conclusion_lines = []
    for row in rows:
        default_rps = float(row["default_noisy_rps_avg"])
        active_rps = float(row["active_noisy_rps_avg"])
        default_p99 = float(row["default_noisy_p99_ms_avg"])
        active_p99 = float(row["active_noisy_p99_ms_avg"])

        lines.append(
            f"| {row['test']} | {default_rps:.3f} | {active_rps:.3f} | {pct_change(active_rps, default_rps)} | "
            f"{default_p99:.3f} | {active_p99:.3f} | {pct_change(active_p99, default_p99)} | "
            f"{row['active_noisy_rps_std']} | {row['active_noisy_p99_ms_std']} |"
        )

        rps_improved = active_rps > default_rps
        p99_improved = active_p99 < default_p99
        if rps_improved or p99_improved:
            conclusion_lines.append(
                f"- `{row['test']}`：相对 `default_noisy`，"
                f"RPS {'提升' if rps_improved else '下降'} {pct_change(active_rps, default_rps)}，"
                f"P99 {'改善' if p99_improved else '变差'} {pct_change(active_p99, default_p99)}。"
            )

    lines.extend(["", "## 自动结论", ""])
    if conclusion_lines:
        lines.extend(conclusion_lines)
    else:
        lines.append("- 当前自动汇总尚未观察到稳定的 active 优势，需要继续调参和重复实验。")

    evidence = collect_agent_evidence(result_dir)
    lines.extend(["", "## Agent 证据摘录", ""])
    if evidence:
        lines.extend(evidence)
    else:
        lines.append("- 当前未在 active agent 快照中提取到 `applied=yes` 的 redis/stress-ng 关键证据。")

    psi_lines = read_psi_summary(result_dir)
    lines.extend(["", "## CPU PSI 摘要", ""])
    if psi_lines:
        lines.extend(psi_lines)
        lines.append("- 当前阶段 PSI 阈值仍采用静态阈值方案，后续计划演进为 baseline 自适应阈值。")
    else:
        lines.append("- 当前结果目录中未找到可用的 CPU PSI 摘要。")

    lines.extend(
        [
            "",
            "## 后续建议",
            "",
            "- 继续增加重复轮数，降低实验波动。",
            "- 继续优化 Redis 与 stress-ng 的控制策略和目标识别规则。",
            "- 补充更多执行证据和图表化材料。",
            "",
        ]
    )

    report_md.write_text("\n".join(lines), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
