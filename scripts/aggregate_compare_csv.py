#!/usr/bin/env python3
import csv
import statistics
import sys
from collections import defaultdict


def stat(values: list[float], fn) -> str:
    return f"{fn(values):.3f}" if values else ""


def stddev(values: list[float]) -> str:
    if len(values) <= 1:
        return "0.000" if values else ""
    return f"{statistics.stdev(values):.3f}"


def main() -> int:
    if len(sys.argv) < 3:
        print("usage: aggregate_compare_csv.py <output.csv> <input1.csv> [input2.csv ...]", file=sys.stderr)
        return 1

    output = sys.argv[1]
    inputs = sys.argv[2:]
    bucket = defaultdict(lambda: defaultdict(list))

    for path in inputs:
        with open(path, newline="", encoding="utf-8") as f:
            reader = csv.DictReader(f)
            for row in reader:
                test = row["test"]
                for key, value in row.items():
                    if key == "test" or value == "":
                        continue
                    bucket[test][key].append(float(value))

    metrics = [
        "baseline_rps",
        "default_noisy_rps",
        "active_noisy_rps",
        "baseline_p99_ms",
        "default_noisy_p99_ms",
        "active_noisy_p99_ms",
    ]

    fieldnames = ["test"]
    for metric in metrics:
        fieldnames.extend([
            f"{metric}_avg",
            f"{metric}_min",
            f"{metric}_max",
            f"{metric}_std",
        ])

    with open(output, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        for test in sorted(bucket):
            row = {"test": test}
            for metric in metrics:
                values = bucket[test].get(metric, [])
                row[f"{metric}_avg"] = stat(values, statistics.mean)
                row[f"{metric}_min"] = stat(values, min)
                row[f"{metric}_max"] = stat(values, max)
                row[f"{metric}_std"] = stddev(values)
            writer.writerow(row)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
