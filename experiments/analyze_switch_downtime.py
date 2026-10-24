#!/usr/bin/env python3
"""Segment wake-up lateness around sched_ext switch downtime.

Usage: analyze_switch_downtime.py <dir-with-watch.tsv-and-probe*.bin> [out.json]

The watcher records sched_ext state in CLOCK_MONOTONIC, and each probe records
(due_ns, late_ns) pairs for an absolute-deadline wake loop in the same clock, so
segments are cut purely by timestamp - no cross-host or cross-clock alignment.

A "gap" is the interval where our scheduler was gone: from the moment
root/ops stopped naming eulerpilot until it named eulerpilot again. That is the
window in which work runs under fair scheduling with no v2 policy applied, so it
is the number an autopilot switch really costs.
"""
import json
import os
import struct
import sys

FIELDS = ("count", "p50_us", "p90_us", "p99_us", "p999_us", "max_us",
          "over_1ms", "over_5ms", "over_20ms")


def stats(values):
    if not values:
        return dict.fromkeys(FIELDS, 0) | {"count": 0}
    ordered = sorted(values)
    n = len(ordered)

    def pct(q):
        idx = min(n - 1, max(0, int(round(q * (n - 1)))))
        return round(ordered[idx] / 1000.0, 3)

    return {
        "count": n,
        "p50_us": pct(0.50), "p90_us": pct(0.90), "p99_us": pct(0.99),
        "p999_us": pct(0.999), "max_us": round(ordered[-1] / 1000.0, 3),
        "over_1ms": sum(1 for v in ordered if v >= 1_000_000),
        "over_5ms": sum(1 for v in ordered if v >= 5_000_000),
        "over_20ms": sum(1 for v in ordered if v >= 20_000_000),
    }


def read_watch(path):
    rows = []
    with open(path, encoding="utf-8") as fp:
        for line in fp:
            if line.startswith("#"):
                continue
            parts = line.rstrip("\n").split("\t")
            if len(parts) < 4:
                continue
            try:
                rows.append((int(parts[0]), parts[1], parts[2], parts[3]))
            except ValueError:
                continue
    return rows


def find_events(rows):
    """Return attach/detach edges of our scheduler, plus enable_seq moves."""
    events = []
    prev_present = None
    prev_seq = None
    for ts, state, ops, seq in rows:
        present = ops == "eulerpilot" and state == "enabled"
        if prev_present is not None and present != prev_present:
            events.append({"at_ns": ts, "kind": "attach" if present else "detach"})
        prev_present = present
        try:
            parsed_seq = int(seq)
        except ValueError:
            parsed_seq = prev_seq
        if prev_seq is not None and parsed_seq is not None and parsed_seq != prev_seq:
            events.append({"at_ns": ts, "kind": "enable_seq", "from": prev_seq,
                           "to": parsed_seq})
        prev_seq = parsed_seq
    return events


def find_gaps(events):
    gaps = []
    open_start = None
    for ev in events:
        if ev["kind"] == "detach":
            open_start = ev["at_ns"]
        elif ev["kind"] == "attach" and open_start is not None:
            gaps.append({"detach_ns": open_start, "attach_ns": ev["at_ns"],
                         "downtime_ms": round((ev["at_ns"] - open_start) / 1e6, 3)})
            open_start = None
    if open_start is not None:
        gaps.append({"detach_ns": open_start, "attach_ns": None,
                     "note": "final detach, no re-attach (teardown)"})
    return gaps


def read_probe(path):
    with open(path, "rb") as fp:
        raw = fp.read()
    n = len(raw) // 16
    if n == 0:
        return [], []
    flat = struct.unpack("<%dQ" % (n * 2), raw[:n * 16])
    return list(flat[0::2]), list(flat[1::2])


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    out_dir = sys.argv[1]
    watch = os.path.join(out_dir, "watch.tsv")
    if not os.path.exists(watch):
        print("missing", watch)
        return 1

    rows = read_watch(watch)
    events = find_events(rows)
    gaps = find_gaps(events)
    attaches = [e["at_ns"] for e in events if e["kind"] == "attach"]
    first_attach = attaches[0] if attaches else None

    probes = {}
    for name in sorted(os.listdir(out_dir)):
        if not (name.startswith("probe") and name.endswith(".bin")):
            continue
        due, late = read_probe(os.path.join(out_dir, name))
        if not due:
            continue
        segments = {}
        if first_attach is not None:
            segments["baseline_no_v2"] = stats(
                [v for d, v in zip(due, late) if d < first_attach - 1_000_000_000])
        for i, gap in enumerate(gaps, 1):
            if gap.get("attach_ns") is None:
                continue
            segments["in_gap_%d" % i] = stats(
                [v for d, v in zip(due, late) if gap["detach_ns"] <= d <= gap["attach_ns"]])
            after_start = gap["attach_ns"] + 1_000_000_000
            segments["after_attach_%d" % i] = stats(
                [v for d, v in zip(due, late)
                 if after_start <= d <= after_start + 5_000_000_000])
        segments["whole_run"] = stats(late)
        probes[name] = segments

    report = {
        "watch_rows": len(rows),
        "sched_ext_enable_seq_moves": [e for e in events if e["kind"] == "enable_seq"],
        "attach_detach_edges": [e for e in events if e["kind"] in ("attach", "detach")],
        "switch_gaps": gaps,
        "probes": probes,
        "note": ("downtime is the interval with no v2 scheduler applied during a "
                 "switch; probes wake every 1ms against an absolute deadline"),
    }
    text = json.dumps(report, indent=2)
    dest = sys.argv[2] if len(sys.argv) > 2 else None
    print(text)
    if dest:
        with open(dest, "w", encoding="utf-8") as fp:
            fp.write(text + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
