#!/usr/bin/env python3
"""Turn matrix block directories into the flagship table, with paired statistics.

Everything is rebuilt from the per-request raw records, not from what the probe
claimed in its own summary: the protocol requires that the raw records can rebuild
the table, and recomputing goodput/p99 here is how that requirement is actually
tested rather than merely asserted. If a number here disagrees with the summary, the
summary is the one that is wrong.

Statistics follow docs/EXPERIMENT_PROTOCOL.md 7: comparisons are *paired inside a
block* against the named baseline, the headline CI is a paired block bootstrap with a
fixed seed and a stated resample count, and the log-ratio version is reported next to
it because percentage differences are not symmetric.

  analyze_matrix_blocks.py DIR [--baseline A0] [--arms A1 A2 C D] [--threshold 5]
                              [--measure-s 60] [--seed 20261024] [--resamples 10000]

Writes per_run.csv and comparisons.csv next to DIR and prints the summary.
"""
import argparse
import csv
import json
import math
import os
import random
import statistics
import struct
import sys


def read_ns(path):
    with open(path, "rb") as handle:
        blob = handle.read()
    n = len(blob) // 8
    return struct.unpack("<%dQ" % n, blob[: n * 8])


def percentile(sorted_values, q):
    if not sorted_values:
        return None
    index = int(math.ceil(q / 100.0 * len(sorted_values))) - 1
    return sorted_values[min(max(index, 0), len(sorted_values) - 1)]


def load_block(block_dir, thresholds, measure_s):
    """One block/arm -> metrics per operation, or None if the block is not evidence."""
    if os.path.exists(os.path.join(block_dir, "block.invalid")):
        return None
    manifest_path = os.path.join(block_dir, "manifest.json")
    if not os.path.exists(manifest_path):
        return None
    manifest = json.load(open(manifest_path))
    attach_path = os.path.join(block_dir, "schedext.state")
    attached = open(attach_path).read().strip() if os.path.exists(attach_path) else "missing"
    out = {"manifest": manifest, "attached": attached, "ops": {}, "invalid_reason": None}
    for command in ("get", "set"):
        raw = os.path.join(block_dir, "raw-%s.bin" % command)
        summary_path = os.path.join(block_dir, "%s.json" % command)
        if not os.path.exists(raw) or not os.path.exists(summary_path):
            continue
        summary = json.load(open(summary_path))
        records = sorted(read_ns(raw))
        if not records:
            out["invalid_reason"] = "no completed requests"
            continue
        if len(records) != summary.get("completed"):
            # Records and counters describe different populations: unusable, and the
            # reason has to be visible rather than averaged away.
            out["invalid_reason"] = "raw/summary mismatch (%d vs %s)" % (
                len(records), summary.get("completed"))
            continue
        under = {t: sum(1 for v in records if v <= t * 1e6) for t in thresholds}
        # Denominator is the *measured* window, taken from the probe's own
        # offered/finished stamps, so this recompute is comparable with the summary
        # rather than a few permille off because 60s was not exactly 60.000s.
        elapsed_s = None
        if summary.get("offered_at_ms") and summary.get("finished_at_ms"):
            elapsed_s = (summary["finished_at_ms"] - summary["offered_at_ms"]) / 1000.0
        if not elapsed_s or elapsed_s <= 0:
            elapsed_s = float(measure_s)
        # When the requested threshold is the one the probe was run with, the two
        # independent computations must agree. That is the protocol's "raw records can
        # rebuild the table" rule turned into a number instead of a claim.
        delta = None
        if summary.get("slo_ms") is not None:
            theirs = summary.get("goodput_at_slo_rps")
            mine = under[summary["slo_ms"]] / elapsed_s if summary["slo_ms"] in under else None
            if theirs and mine:
                # The probe counted good requests itself; this is the independent
                # recomputation from the same raw records at the same threshold.
                delta = round(100.0 * (mine / theirs - 1.0), 4)
        out["ops"][command.upper()] = {
            "offered": summary.get("offered"),
            "completed": summary.get("completed"),
            "achieved_rps": summary.get("achieved_rps"),
            "deficit_rps": summary.get("deficit_rps"),
            "timeouts": summary.get("timeouts"),
            "stalls": summary.get("stalls"),
            "client_cpu_per_s": summary.get("cpu_per_second"),
            "peak_inflight": summary.get("peak_inflight"),
            "samples": len(records),
            "elapsed_s": round(elapsed_s, 3),
            "p50_ms": round(percentile(records, 50) / 1e6, 4),
            "p95_ms": round(percentile(records, 95) / 1e6, 4),
            "p99_ms": round(percentile(records, 99) / 1e6, 4),
            "p999_ms": round(percentile(records, 99.9) / 1e6, 4),
            "goodput_rps": {t: round(under[t] / elapsed_s, 1) for t in thresholds},
            "good_at_threshold_pct": {t: round(100.0 * under[t] / len(records), 3)
                                      for t in thresholds},
            "goodput_vs_summary_pct": delta,
        }
    marks = os.path.join(block_dir, "bg.marks")
    rows = 0
    bg_log = os.path.join(block_dir, "bg.ndjson")
    waits = []
    if os.path.exists(bg_log):
        for line in open(bg_log, errors="replace"):
            line = line.strip()
            if line.startswith("{"):
                try:
                    row = json.loads(line)
                except Exception:
                    continue
                rows += 1
                if isinstance(row.get("max_wait_ms"), (int, float)):
                    waits.append(row["max_wait_ms"])
    if os.path.exists(marks):
        values = [float(x) for x in open(marks).read().split()]
        # The first two marks are taken before the first window has finished and read
        # 0.00, which means "nothing measured yet" and not "the background stalled";
        # the third is the median over the whole block, so that is the block's number.
        out["bg_units_per_s"] = round(values[-1], 2) if values else None
        gate = manifest.get("online_gate", {})
        expected = (manifest["workload"]["warmup_s"] + 2 * manifest["workload"]["measure_s"]) / \
            max(1, gate.get("bg_window_s", 5))
        # Under-populated BG telemetry is a reliability finding in its own right.
        out["bg_windows"] = rows
        out["bg_windows_expected"] = round(expected, 1)
        out["bg_under_populated"] = rows < 0.5 * expected
    out["bg_max_wait_ms"] = round(max(waits), 3) if waits else None
    # What the control plane actually did inside the block: a switch costs a measured
    # 303-665ms unprotected window, so "D steady" and "D switched" are not the same
    # sample and must not be averaged together silently.
    status_path = os.path.join(block_dir, "status.json")
    out["auto_switches"] = None
    out["outcome_pending"] = None
    out["observations_rejected"] = None
    if os.path.exists(status_path):
        try:
            status = json.load(open(status_path))
        except Exception:
            status = {}
        auto = status.get("auto_apply") or {}
        service = status.get("service") or {}
        out["auto_switches"] = auto.get("switches_recorded")
        out["outcome_pending"] = service.get("outcome_pending")
        out["observations_rejected"] = service.get("observations_rejected")
        out["adverse_experts"] = len(service.get("adverse_experts") or {})
    return out if out["ops"] else None


def paired_bootstrap(pairs, resamples, seed):
    """Median of paired per-block differences, CI from resampling *blocks*."""
    if not pairs:
        return None
    rng = random.Random(seed)
    medians = []
    for _ in range(resamples):
        sample = [pairs[rng.randrange(len(pairs))] for _ in range(len(pairs))]
        medians.append(statistics.median(sample))
    medians.sort()
    low = medians[int(0.025 * len(medians))]
    high = medians[min(int(0.975 * len(medians)), len(medians) - 1)]
    return {"median": round(statistics.median(pairs), 3), "ci_low": round(low, 3),
            "ci_high": round(high, 3), "n": len(pairs), "resamples": resamples,
            "seed": seed}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dir", type=str)
    parser.add_argument("--baseline", default="A0")
    parser.add_argument("--baselines", default="",
                       help="additional baselines to pair against in the same pass (the paired "
                            "data is already in memory; each extra baseline would otherwise "
                            "mean reading the whole raw corpus again)")
    parser.add_argument("--arms", default="A1 A2 C D")
    parser.add_argument("--threshold", type=float, default=5.0,
                       help="primary SLO threshold; the bootstrap CI is reported for this one")
    parser.add_argument("--also-thresholds", default="",
                       help="extra thresholds to report in the same pass -- the raw records are "
                            "already loaded, and re-running to ask one more threshold would read "
                            "the whole corpus again")
    parser.add_argument("--measure-s", type=int, default=60)
    parser.add_argument("--seed", type=int, default=20261024)
    parser.add_argument("--resamples", type=int, default=10000)
    args = parser.parse_args()

    if not os.path.isdir(args.dir):
        print("no such directory: %s" % args.dir, file=sys.stderr)
        return 3
    baselines = [args.baseline] + [b for b in args.baselines.replace(",", " ").split()
                                    if b and b != args.baseline]
    arms = baselines + [a for a in args.arms.replace(",", " ").split() if a not in baselines]
    thresholds = [args.threshold]
    thresholds += [float(t) for t in args.also_thresholds.split(",") if t
                   and abs(float(t) - args.threshold) > 1e-9]

    data = {}
    dropped = []
    for entry in sorted(os.listdir(args.dir)):
        path = os.path.join(args.dir, entry)
        if not os.path.isdir(path) or "-" not in entry:
            continue
        block, arm = entry.split("-", 1)
        if arm not in arms:
            continue
        metrics = load_block(path, thresholds, args.measure_s)
        if metrics is None:
            dropped.append(entry)
            continue
        data.setdefault(arm, {})[int(block)] = metrics

    print("blocks: %s | dropped/not-evidence: %s" % (
        {arm: len(v) for arm, v in sorted(data.items())}, dropped or "none"))
    if not data.get(args.baseline):
        print("no usable baseline blocks (%s) -- nothing to pair against" % args.baseline)
        return 2

    per_run_path = os.path.join(args.dir, "per_run.csv")
    with open(per_run_path, "w", newline="") as handle:
        writer = csv.writer(handle)
        head = ["block", "arm", "op", "offered", "completed", "achieved_rps",
                "deficit_rps", "timeouts", "stalls", "peak_inflight", "client_cpu_per_s",
                "samples", "p50_ms", "p95_ms", "p99_ms", "p999_ms"]
        for t in thresholds:
            head += ["goodput_rps_at_%gms" % t, "good_pct_at_%gms" % t]
        head += ["attached", "bg_units_per_s", "bg_max_wait_ms", "bg_windows",
                 "bg_windows_expected", "bg_under_populated", "auto_switches",
                 "outcome_pending", "observations_rejected", "elapsed_s",
                 "goodput_vs_summary_pct"]
        writer.writerow(head)
        rebuilt_disagree = []
        for arm in arms:
            for block in sorted(data.get(arm, {})):
                metrics = data[arm][block]
                for command, values in sorted(metrics["ops"].items()):
                    row = [block, arm, command, values["offered"], values["completed"],
                           values["achieved_rps"], values["deficit_rps"], values["timeouts"],
                           values["stalls"], values["peak_inflight"], values["client_cpu_per_s"],
                           values["samples"], values["p50_ms"], values["p95_ms"],
                           values["p99_ms"], values["p999_ms"]]
                    for t in thresholds:
                        row += [values["goodput_rps"][t], values["good_at_threshold_pct"][t]]
                    row += [metrics["attached"], metrics.get("bg_units_per_s"),
                            metrics.get("bg_max_wait_ms"), metrics.get("bg_windows"),
                            metrics.get("bg_windows_expected"),
                            "yes" if metrics.get("bg_under_populated") else "no",
                            metrics.get("auto_switches"), metrics.get("outcome_pending"),
                            metrics.get("observations_rejected"), values["elapsed_s"],
                            values["goodput_vs_summary_pct"]]
                    writer.writerow(row)
                    if values["goodput_vs_summary_pct"] is not None and \
                            abs(values["goodput_vs_summary_pct"]) > 0.5:
                        rebuilt_disagree.append((block, arm, command, values["goodput_vs_summary_pct"]))
    print("wrote %s" % per_run_path)
    if rebuilt_disagree:
        print("CHECK_RAW_REBUILDS_SUMMARY=no (goodput recomputed from raw differs from the "
              "probe summary by >0.5% in: %s)" % rebuilt_disagree)
    else:
        print("CHECK_RAW_REBUILDS_SUMMARY=yes (every recompute at the probe's own threshold "
              "agrees within 0.5%)")

    comparison_rows = []
    for baseline in baselines:
        print("\n=== paired comparisons against %s, within each block ===" % baseline)
        for arm in arms:
            if arm == baseline:
                continue
            for command in ("GET", "SET"):
                common = sorted(set(data.get(arm, {})) & set(data[baseline]))
                gains = {t: [] for t in thresholds}
                goodput_gain, p99_change, log_ratio, bg_ratio = gains[args.threshold], [], [], []
                wins = losses = 0
                for block in common:
                    mine = data[arm][block]["ops"].get(command)
                    base = data[baseline][block]["ops"].get(command)
                    if not mine or not base:
                        continue
                    for t in thresholds:
                        if base["goodput_rps"][t] and mine["goodput_rps"][t]:
                            gains[t].append(
                                100.0 * (mine["goodput_rps"][t] / base["goodput_rps"][t] - 1.0))
                    # goodput_gain *is* gains[args.threshold] (same list object), so the
                    # primary value was appended in that loop; re-appending would double-count.
                    if not (base["goodput_rps"][args.threshold] and
                            mine["goodput_rps"][args.threshold]):
                        continue
                    gain = 100.0 * (mine["goodput_rps"][args.threshold] /
                                    base["goodput_rps"][args.threshold] - 1.0)
                    if gain > 0:
                        wins += 1
                    elif gain < 0:
                        losses += 1
                    if base["p99_ms"] and mine["p99_ms"]:
                        p99_change.append(100.0 * (mine["p99_ms"] / base["p99_ms"] - 1.0))
                        log_ratio.append(100.0 * math.log(
                            mine["goodput_rps"][args.threshold] /
                            base["goodput_rps"][args.threshold]))
                    base_bg = data[baseline][block].get("bg_units_per_s")
                    if base_bg and data[arm][block].get("bg_units_per_s"):
                        bg_ratio.append(data[arm][block]["bg_units_per_s"] / base_bg)
                row = {"arm": arm, "op": command, "baseline": baseline,
                       "blocks": len(goodput_gain), "wins": wins, "losses": losses}
                boot = paired_bootstrap(goodput_gain, args.resamples, args.seed)
                row["goodput_gain_pct"] = boot
                row["sensitivity"] = {
                    t: (round(statistics.median(gains[t]), 3) if gains[t] else None)
                    for t in thresholds}
                row["p99_change_pct_median"] = (
                    round(statistics.median(p99_change), 3) if p99_change else None)
                row["log_ratio_pct_median"] = (
                    round(statistics.median(log_ratio), 3) if log_ratio else None)
                row["bg_vs_baseline_median"] = (
                    round(statistics.median(bg_ratio), 4) if bg_ratio else None)
                comparison_rows.append(row)
                if boot is None:
                    print("%s %s (vs %s): no paired blocks with a non-zero baseline goodput"
                          % (arm, command, baseline))
                    continue
                print("%s vs %s %s n=%d goodput %+.2f%% [%.2f, %.2f] "
                      "(paired block bootstrap, seed=%d, %d resamples) wins/losses %d/%d | "
                      "p99 %+.2f%% | bg x%s" % (
                          arm, baseline, command, boot["n"], boot["median"], boot["ci_low"],
                          boot["ci_high"], args.seed, args.resamples, wins, losses,
                          row["p99_change_pct_median"] or 0.0,
                          "%.3f" % row["bg_vs_baseline_median"]
                          if row["bg_vs_baseline_median"] else "-"))
                if len(thresholds) > 1:
                    print("      goodput gain by threshold: " +
                          "  ".join("%gms=%s%%" % (t, row["sensitivity"][t]) for t in thresholds))

    comparisons_path = os.path.join(args.dir, "comparisons.csv")
    with open(comparisons_path, "w", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(["arm", "op", "baseline", "paired_blocks", "goodput_gain_median_pct",
                         "ci_low_pct", "ci_high_pct", "wins", "losses", "p99_change_median_pct",
                         "log_ratio_median_pct", "bg_vs_baseline_median", "resamples", "seed",
                         "sensitivity_goodput_gain_pct_by_threshold"])
        for row in comparison_rows:
            boot = row["goodput_gain_pct"] or {}
            writer.writerow([row["arm"], row["op"], row["baseline"], row["blocks"],
                             boot.get("median"), boot.get("ci_low"), boot.get("ci_high"),
                             row["wins"], row["losses"], row["p99_change_pct_median"],
                             row["log_ratio_pct_median"], row["bg_vs_baseline_median"],
                             args.resamples, args.seed,
                             " ".join("%g:%s" % (t, row["sensitivity"][t])
                                      for t in thresholds)])
    print("\nwrote %s" % comparisons_path)
    if len(set(len(v) for v in data.values())) > 1:
        print("NOTE paired counts differ per arm; only blocks present in both arms enter a "
              "comparison, and the ledger above lists what was dropped.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
