#!/usr/bin/env python3
import re
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: extract_wrk_metrics.py <wrk_output.txt> <summary.csv>", file=sys.stderr)
        return 1

    wrk_output = Path(sys.argv[1]).read_text(encoding="utf-8", errors="ignore")
    summary_csv = Path(sys.argv[2])

    req_match = re.search(r"Requests/sec:\s+([0-9.]+)", wrk_output)
    latency_match = re.search(r"Latency\s+([0-9.]+\w+)\s+([0-9.]+\w+)\s+([0-9.]+\w+)", wrk_output)
    p99_match = re.search(r"\s+99%\s+([0-9.]+\w+)", wrk_output)

    req = req_match.group(1) if req_match else ""
    avg_latency = latency_match.group(1) if latency_match else ""
    stdev_latency = latency_match.group(2) if latency_match else ""
    max_latency = latency_match.group(3) if latency_match else ""
    p99 = p99_match.group(1) if p99_match else ""

    summary_csv.write_text(
        "requests_per_sec,avg_latency,stdev_latency,max_latency,p99_latency\n"
        f"{req},{avg_latency},{stdev_latency},{max_latency},{p99}\n",
        encoding="utf-8",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
