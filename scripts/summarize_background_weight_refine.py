#!/usr/bin/env python3
import csv
import sys
from pathlib import Path


def pct_change(new: float, old: float) -> float:
    if old == 0:
        return 0.0
    return ((new - old) / old) * 100.0


def read_compare_summary(path: Path) -> dict[str, dict[str, float]]:
    with path.open(newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        return {
            row["test"]: {
                "default_rps": float(row["default_noisy_rps_avg"]),
                "active_rps": float(row["active_noisy_rps_avg"]),
                "default_p99": float(row["default_noisy_p99_ms_avg"]),
                "active_p99": float(row["active_noisy_p99_ms_avg"]),
            }
            for row in reader
        }


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: summarize_background_weight_refine.py <index.csv> <report.md>", file=sys.stderr)
        return 1

    index_csv = Path(sys.argv[1])
    report_md = Path(sys.argv[2])
    rows = []

    with index_csv.open(newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        rows = list(reader)

    lines = [
        "# background_weight 微调汇总",
        "",
        "## 说明",
        "",
        "- 当前固定 `latency_weight=1000`。",
        "- 只微调 `background_weight`，用于观察对 `INCR/GET/SET` 的平衡影响。",
        "- `wins` 表示同时满足“RPS 不下降且 P99 不变差”的测试项数量。",
        "",
        "| background_weight | cpu_psi_threshold | latency_wait_threshold_ns | background_runtime_threshold_ns | wins | 结果目录 |",
        "| ---: | ---: | ---: | ---: | ---: | --- |",
    ]

    detail_lines = ["", "## 关键测试项对比", ""]

    for row in rows:
        result_dir = Path(row["result_dir"])
        compare = read_compare_summary(result_dir / "compare_summary_avg.csv")
        wins = 0
        for metrics in compare.values():
            if metrics["active_rps"] >= metrics["default_rps"] and metrics["active_p99"] <= metrics["default_p99"]:
                wins += 1

        lines.append(
            f"| {row['background_weight']} | {row['cpu_psi_threshold']} | {row['latency_wait_threshold_ns']} | {row['background_runtime_threshold_ns']} | {wins} | {row['result_dir']} |"
        )

        detail_lines.append(
            f"### 权重组合：background_weight={row['background_weight']}，cpu_psi={row['cpu_psi_threshold']}，latency_wait_ns={row['latency_wait_threshold_ns']}，background_runtime_ns={row['background_runtime_threshold_ns']}"
        )
        detail_lines.append("")
        detail_lines.append("| 测试项 | RPS变化 | P99变化 |")
        detail_lines.append("| --- | ---: | ---: |")
        for test in ("GET", "SET", "INCR", "PING_INLINE"):
            metrics = compare.get(test)
            if not metrics:
                continue
            rps_change = pct_change(metrics["active_rps"], metrics["default_rps"])
            p99_change = pct_change(metrics["active_p99"], metrics["default_p99"])
            detail_lines.append(f"| {test} | {rps_change:.2f}% | {p99_change:.2f}% |")
        detail_lines.append("")

    report_md.write_text("\n".join(lines + detail_lines), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
