#!/usr/bin/env python3
import csv
import json
import statistics
import sys
from collections import defaultdict
from pathlib import Path


def parse_latency_us(value: str) -> float:
    value = value.strip()
    if not value:
        return 0.0
    if value.endswith("us"):
        return float(value[:-2]) / 1000.0
    if value.endswith("ms"):
        return float(value[:-2])
    if value.endswith("s"):
        return float(value[:-1]) * 1000.0
    return float(value)


def read_summary(path: Path) -> dict[str, float]:
    with path.open(newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            return {
                "requests_per_sec": float(row["requests_per_sec"]),
                "p99_latency_ms": parse_latency_us(row["p99_latency"]),
            }
    return {"requests_per_sec": 0.0, "p99_latency_ms": 0.0}


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
        print("usage: nginx_compare_summary.py <input.json> <output.csv>", file=sys.stderr)
        return 1

    input_json = Path(sys.argv[1])
    output_csv = Path(sys.argv[2])
    manifest = json.loads(input_json.read_text(encoding="utf-8"))

    labels = manifest["labels"]
    bucket: dict[str, list[float]] = defaultdict(list)

    for label in labels:
        for path_str in manifest["summary_paths"].get(label, []):
            summary = read_summary(Path(path_str))
            bucket[f"{label}_rps"].append(summary["requests_per_sec"])
            bucket[f"{label}_p99_ms"].append(summary["p99_latency_ms"])

    fieldnames = []
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
        row = {}
        for label in labels:
            row[f"{label}_rps_avg"] = mean(bucket[f"{label}_rps"])
            row[f"{label}_rps_std"] = stddev(bucket[f"{label}_rps"])
            row[f"{label}_p99_ms_avg"] = mean(bucket[f"{label}_p99_ms"])
            row[f"{label}_p99_ms_std"] = stddev(bucket[f"{label}_p99_ms"])
        writer.writerow(row)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
