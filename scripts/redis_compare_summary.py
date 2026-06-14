#!/usr/bin/env python3
import csv
import json
import statistics
import sys
from collections import defaultdict
from pathlib import Path


def read_summary(path: Path) -> dict[str, dict[str, float]]:
    rows: dict[str, dict[str, float]] = {}
    with path.open(newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            test = row["test"]
            rows[test] = {
                "rps": float(row["rps"]),
                "p99_latency_ms": float(row["p99_latency_ms"]),
            }
    return rows


def mean(values: list[float]) -> str:
    return f"{statistics.mean(values):.3f}" if values else ""


def stddev(values: list[float]) -> str:
    if not values:
        return ""
    if len(values) == 1:
        return "0.000"
    return f"{statistics.stdev(values):.3f}"


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: redis_compare_summary.py <input.json> <output.csv>", file=sys.stderr)
        return 1

    input_json = Path(sys.argv[1])
    output_csv = Path(sys.argv[2])
    manifest = json.loads(input_json.read_text(encoding="utf-8"))

    bucket: dict[str, dict[str, list[float]]] = defaultdict(lambda: defaultdict(list))
    labels: list[str] = manifest["labels"]
    summary_paths: dict[str, list[str]] = manifest["summary_paths"]

    tests: set[str] = set()
    for label in labels:
        for path_str in summary_paths.get(label, []):
            summary = read_summary(Path(path_str))
            for test, metrics in summary.items():
                tests.add(test)
                bucket[test][f"{label}_rps"].append(metrics["rps"])
                bucket[test][f"{label}_p99_ms"].append(metrics["p99_latency_ms"])

    fieldnames = ["test"]
    for label in labels:
        fieldnames.extend(
            [
                f"{label}_rps_avg",
                f"{label}_rps_std",
                f"{label}_p99_ms_avg",
                f"{label}_p99_ms_std",
            ]
        )

    with output_csv.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        for test in sorted(tests):
            row = {"test": test}
            for label in labels:
                rps_values = bucket[test].get(f"{label}_rps", [])
                p99_values = bucket[test].get(f"{label}_p99_ms", [])
                row[f"{label}_rps_avg"] = mean(rps_values)
                row[f"{label}_rps_std"] = stddev(rps_values)
                row[f"{label}_p99_ms_avg"] = mean(p99_values)
                row[f"{label}_p99_ms_std"] = stddev(p99_values)
            writer.writerow(row)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
