#!/usr/bin/env python3
"""Rebuild latency statistics from redis_openloop_probe --raw files.

The probe deliberately reports no percentiles: it emits one u64 latency (ns) per
completion plus a counters line, and this script is what turns those records into
the numbers the table uses. Keeping the split is what makes the protocol's rule
("the raw records must be able to rebuild the table") checkable -- if a goodput or
a p99 here disagrees with the summary, the summary is wrong.

It also answers the question the flagship freeze depends on: what does goodput look
like at each candidate SLO threshold, for every arm, from the same records. A
threshold chosen from one run's own numbers is a frozen parameter, not a result.

  analyze_openloop_raw.py DIR [--thresholds 1,2,5,10] [--top 8]

DIR holds <block>-<arm>/raw-{get,set}.bin as written by redis_matrix_sp3.sh.
"""
import argparse
import json
import os
import statistics
import struct
import sys


def read_ns(path):
    with open(path, "rb") as handle:
        blob = handle.read()
    n = len(blob) // 8
    return struct.unpack("<%dQ" % n, blob[: n * 8])


def percentile(sorted_values, q):
    """Nearest-rank on the sorted records: no interpolation, no invented tail."""
    if not sorted_values:
        return None
    import math

    index = int(math.ceil(q / 100.0 * len(sorted_values))) - 1
    index = min(max(index, 0), len(sorted_values) - 1)
    return sorted_values[index]


def stats(records, thresholds):
    values = sorted(records)
    total = len(values)
    out = {
        "samples": total,
        "p50_ms": round(percentile(values, 50) / 1e6, 4) if total else None,
        "p95_ms": round(percentile(values, 95) / 1e6, 4) if total else None,
        "p99_ms": round(percentile(values, 99) / 1e6, 4) if total else None,
        "p999_ms": round(percentile(values, 99.9) / 1e6, 4) if total else None,
        "max_ms": round(values[-1] / 1e6, 4) if total else None,
        "mean_ms": round(sum(values) / total / 1e6, 4) if total else None,
    }
    for threshold in thresholds:
        under = sum(1 for v in values if v <= threshold * 1e6)
        out["good_at_%gms_pct" % threshold] = round(100.0 * under / total, 2) if total else None
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dir", type=str)
    parser.add_argument("--thresholds", default="1,2,5,10")
    parser.add_argument("--top", type=int, default=0, help="print the N worst records in ms")
    args = parser.parse_args()
    thresholds = [float(x) for x in args.thresholds.split(",") if x]

    rows = []
    if not os.path.isdir(args.dir):
        print("no such directory: %s" % args.dir, file=sys.stderr)
        return 3
    for entry in sorted(os.listdir(args.dir)):
        block_dir = os.path.join(args.dir, entry)
        if not os.path.isdir(block_dir) or "-" not in entry:
            continue
        block, arm = entry.split("-", 1)
        for command in ("get", "set"):
            raw = os.path.join(block_dir, "raw-%s.bin" % command)
            if not os.path.exists(raw):
                continue
            summary_path = os.path.join(block_dir, "%s.json" % command)
            summary = json.load(open(summary_path)) if os.path.exists(summary_path) else {}
            records = read_ns(raw)
            row = {"block": block, "arm": arm, "op": command.upper()}
            row.update(stats(records, thresholds))
            row["offered"] = summary.get("offered")
            row["completed"] = summary.get("completed")
            row["achieved_rps"] = summary.get("achieved_rps")
            row["summary_goodput"] = summary.get("goodput_at_slo_rps")
            row["slo_ms_in_summary"] = summary.get("slo_ms")
            # A raw file whose length is not the completed count means the summary and
            # the records describe different populations -- that block is not usable.
            row["records_vs_completed"] = "match" if row["completed"] == row["samples"] else "MISMATCH"
            rows.append(row)
            if args.top:
                worst = sorted(records, reverse=True)[: args.top]
                row["worst_ms"] = [round(v / 1e6, 3) for v in worst]

    if not rows:
        print("no raw-*.bin found under %s" % args.dir)
        return 1

    print("%-6s %-4s %-4s %8s %8s %8s %8s %9s %s" % (
        "block", "arm", "op", "p50ms", "p95ms", "p99ms", "p99.9", "samples",
        "good% @ " + "/".join("%g" % t for t in thresholds)))
    for row in rows:
        print("%-6s %-4s %-4s %8s %8s %8s %8s %9d %s  [%s]" % (
            row["block"], row["arm"], row["op"], row["p50_ms"], row["p95_ms"], row["p99_ms"],
            row["p999_ms"], row["samples"],
            "/".join(str(row.get("good_at_%gms_pct" % t)) for t in thresholds),
            row["records_vs_completed"]))

    # Per-arm medians across blocks: what a table row will actually show.
    print()
    for arm in sorted({row["arm"] for row in rows}):
        for command in ("GET", "SET"):
            subset = [row for row in rows if row["arm"] == arm and row["op"] == command]
            if not subset:
                continue
            values = lambda key: [r[key] for r in subset if r[key] is not None]
            med = lambda key: (round(statistics.median(values(key)), 4) if values(key) else None)
            line = "%s %s n=%d p50 %s p99 %s" % (arm, command, len(subset), med("p50_ms"), med("p99_ms"))
            line += " | " + " ".join("%gms:%s%%" % (t, med("good_at_%gms_pct" % t)) for t in thresholds)
            print(line)
    mismatched = [row for row in rows if row["records_vs_completed"] != "match"]
    if mismatched:
        print("\nBLOCKS WITH records_vs_completed MISMATCH: %s" %
              [(row["block"], row["arm"], row["op"]) for row in mismatched], file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
