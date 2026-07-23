#!/usr/bin/env python3
import json
import subprocess
import sys
from pathlib import Path


STAT_NAMES = {
    0: "class_hits_normal",
    1: "class_hits_latency",
    2: "class_hits_batch",
    3: "class_hits_background",
    4: "class_map_hit",
    5: "class_map_miss",
    6: "invalid_class",
    7: "enqueue_shared",
    8: "enqueue_latency",
    9: "enqueue_batch",
    10: "enqueue_background",
    11: "dispatch_shared",
    12: "dispatch_latency",
    13: "dispatch_batch",
    14: "dispatch_background",
    15: "running_shared",
    16: "running_latency",
    17: "running_batch",
    18: "running_background",
    19: "shared_fallback",
    20: "starvation_guard",
    21: "bg_consumed_slice_total",
    22: "direct_local_latency",
}


def dump_stats() -> dict[str, int]:
    candidate_paths = [
        "/sys/fs/bpf/eulerpilot/scx_eulerpilot/v1/stats",
        "/sys/fs/bpf/stats",
    ]
    last_error = None
    raw = None
    for path in candidate_paths:
        try:
            proc = subprocess.run(
                ["bpftool", "-j", "map", "dump", "pinned", path],
                check=True,
                text=True,
                capture_output=True,
            )
            raw = json.loads(proc.stdout)
            break
        except subprocess.CalledProcessError as exc:
            last_error = exc
            continue

    if raw is None:
        raise last_error if last_error else RuntimeError("failed to locate pinned stats map")

    stats: dict[str, int] = {}
    for entry in raw:
        key_bytes = entry["key"]
        if isinstance(key_bytes, list):
            key = int.from_bytes(bytes(int(x, 16) for x in key_bytes), "little")
        else:
            key = int(key_bytes)

        if "values" in entry:
            total = 0
            for cpu_entry in entry["values"]:
                raw_value = cpu_entry["value"]
                if isinstance(raw_value, list):
                    total += int.from_bytes(bytes(int(x, 16) for x in raw_value), "little")
                else:
                    total += int(raw_value)
            value = total
        else:
            raw_value = entry["value"]
            if isinstance(raw_value, list):
                value = int.from_bytes(bytes(int(x, 16) for x in raw_value), "little")
            else:
                value = int(raw_value)

        stats[STAT_NAMES.get(key, f"stat_{key}")] = value
    return stats


def main() -> int:
    if len(sys.argv) not in (2, 3):
        print("usage: collect_scx_stats.py <output.json> [baseline.json]", file=sys.stderr)
        return 1

    output = Path(sys.argv[1])
    current = dump_stats()

    if len(sys.argv) == 3:
        baseline = json.loads(Path(sys.argv[2]).read_text(encoding="utf-8"))
        delta = {k: current.get(k, 0) - baseline.get(k, 0) for k in STAT_NAMES.values()}
        output.write_text(json.dumps(delta, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    else:
        output.write_text(json.dumps(current, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
