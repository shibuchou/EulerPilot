#!/usr/bin/env bash
# SP3 fairness probe: one CPU, continuous LAT + COMP + NORMAL + BG.
# Requires an idle sched_ext host and root. Not part of ctest; run manually.
set -euo pipefail

EXECUTORD="${1:?usage: fairness_sp3.sh <ep-executord> <ep> <loader> [out.json]}"
EP="${2:?usage: fairness_sp3.sh <ep-executord> <ep> <loader> [out.json]}"
LOADER="${3:?usage: fairness_sp3.sh <ep-executord> <ep> <loader> [out.json]}"
OUT="${4:-fairness-sp3.json}"
CTL="$(dirname "$LOADER")/ep-schedctl"
[ "$(id -u)" = "0" ] || { echo "requires root" >&2; exit 77; }
[ -x "$LOADER" ] && [ -x "$CTL" ] && [ -x "$EXECUTORD" ] || exit 77
[ "$(cat /sys/kernel/sched_ext/state 2>/dev/null)" = "disabled" ] ||
  { echo "sched_ext is not idle" >&2; exit 1; }

TMP="$(mktemp -d /tmp/ep-fair.XXXXXX)"
STATE="$TMP/state"
SOCK="$TMP/executor.sock"
FG="/sys/fs/cgroup/ep-fair-fg-$$"
BG_CG="/sys/fs/cgroup/ep-fair-bg-$$"
POLICY="$TMP/policy.json"
DAEMON_PID=""
PINDIR=""
PROBES=()
mkdir -p "$FG" "$BG_CG"

cleanup() {
  local result=$?
  for pid in "${PROBES[@]:-}"; do kill -KILL "$pid" 2>/dev/null || true; done
  if [ -n "$DAEMON_PID" ] && kill -0 "$DAEMON_PID" 2>/dev/null; then
    kill -TERM "$DAEMON_PID" 2>/dev/null || true
    wait "$DAEMON_PID" 2>/dev/null || true
  fi
  for _ in $(seq 1 100); do
    [ "$(cat /sys/kernel/sched_ext/state 2>/dev/null)" = "disabled" ] && break
    sleep 0.05
  done
  rmdir "$FG" "$BG_CG" 2>/dev/null || true
  if [ -n "$PINDIR" ] && [ -d "$PINDIR" ] &&
     [ "$(cat /sys/kernel/sched_ext/state 2>/dev/null)" = "disabled" ]; then
    rm -rf "$PINDIR"
  fi
  if [ "$result" -eq 0 ]; then rm -rf "$TMP"; else echo "[FAIL] evidence retained: $TMP" >&2; fi
}
trap cleanup EXIT

python3 - "$POLICY" "$FG" "$BG_CG" <<'PY'
import json, sys
policy = {
  "apiVersion": "eulerpilot.io/v1alpha1",
  "kind": "SchedulingPolicy",
  "metadata": {"name": "p1-fairness"},
  "spec": {
    "scope": "scheduler-instance",
    "mode": "class",
    "expert": "latency-balanced",
    "source": "human",
    "targets": [
      {"kind": "cgroup", "path": sys.argv[2], "role": "FOREGROUND", "role_source": "explicit"},
      {"kind": "pid", "pid": 1, "role": "FOREGROUND", "role_source": "explicit"},
      {"kind": "cgroup", "path": sys.argv[3], "role": "BACKGROUND", "role_source": "explicit"}
    ],
    "parameters": {
      "lat_slice_ns": 1000000, "comp_slice_ns": 4000000,
      "cache_slice_ns": 4000000, "bg_slice_ns": 2000000,
      "preempt_threshold_ns": 500000, "bg_vtime_pct": 200,
      "llc_affinity": False
    },
    "safety": {
      "min_dwell_ms": 5000, "max_observation_age_ms": 1000,
      "heartbeat_period_ms": 250, "heartbeat_timeout_ms": 2000,
      "policy_ttl_ms": 60000, "quiet_max_regression_pct": 5,
      "evaluation_min_bg_progress_ratio": 0.5, "bg_work_unit": "requests",
      "bg_progress_window_ms": 5000, "min_bg_work_units_per_sec": 1,
      "max_bg_wait_ms": 1000, "fallback": "verified-safe-profile",
      "allow_affinity_change": False
    }
  }
}
json.dump(policy, open(sys.argv[1], "w"))
PY

gcc -O2 -o "$TMP/probe" "$(dirname "$0")/fairness_probe.c"
"$EXECUTORD" --state-dir "$STATE" --socket "$SOCK" --skill sched --loader "$LOADER" \
  --sched-ctl "$CTL" >"$TMP/daemon.log" 2>&1 &
DAEMON_PID=$!
for _ in $(seq 1 100); do
  "$EP" status --socket "$SOCK" >/dev/null 2>&1 && break
  sleep 0.05
done
apply="$("$EP" apply --file "$POLICY" --idempotency-key idem-fairness-0001 --socket "$SOCK")"
python3 - "$apply" <<'PY'
import json, sys
r = json.loads(sys.argv[1])
assert r.get("status") == "VERIFIED", r
PY
INSTANCE="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["instance_id"])' "$STATE/state.json")"
PINDIR="/sys/fs/bpf/eulerpilot-v2/$INSTANCE"
BUDGET="${FAIR_BUDGET:-60}"
LAT_SLICE="${FAIR_LAT_SLICE_NS:-}"
COMP_SLICE="${FAIR_COMP_SLICE_NS:-}"
BG_SLICE="${FAIR_BG_SLICE_NS:-}"
# lat_budget_pct only caps LAT from above: with a 1ms LAT slice against 4ms
# competitors the share settles at the slice-length ratio (~31%) and the
# throttle counter never fires. The 2026-10-22 three-arm run measured
# 30.8%/51.3%/62.7% for LAT slices of 1/4/8ms, so the reservation has to be
# expressed as a slice quota. Others average comp_slice_ns, so
# lat_slice = comp_slice * pct / (100 - pct) targets the budget.
POLICY_COMP_SLICE_NS=4000000
[ -z "$LAT_SLICE" ] && LAT_SLICE=$(( POLICY_COMP_SLICE_NS * BUDGET / (100 - BUDGET) ))
SLICE_JSON="\"lat_slice_ns\":${LAT_SLICE},"
[ -n "$COMP_SLICE" ] && SLICE_JSON="${SLICE_JSON}\"comp_slice_ns\":${COMP_SLICE},"
[ -n "$BG_SLICE" ] && SLICE_JSON="${SLICE_JSON}\"bg_slice_ns\":${BG_SLICE},"
"$CTL" set --pin-dir "$PINDIR" --json "{${SLICE_JSON}\"lat_budget_pct\":$BUDGET}" >/dev/null
CPU=$(( $(nproc) - 1 ))
[ "$CPU" -ge 0 ] || CPU=0

taskset -c "$CPU" "$TMP/probe" 4 >"$TMP/lat.json" & LAT=$!
taskset -c "$CPU" "$TMP/probe" 4 >"$TMP/comp.json" & COMP=$!
taskset -c "$CPU" "$TMP/probe" 4 >"$TMP/normal.json" & NORMAL=$!
taskset -c "$CPU" "$TMP/probe" 4 >"$TMP/bg.json" & BG=$!
PROBES=("$LAT" "$COMP" "$NORMAL" "$BG")
sleep 0.3
"$CTL" class-set --pin-dir "$PINDIR" --pid "$LAT" --class LAT --confidence 1000 >/dev/null
"$CTL" class-set --pin-dir "$PINDIR" --pid "$COMP" --class COMP --confidence 1000 >/dev/null
"$CTL" class-set --pin-dir "$PINDIR" --pid "$BG" --class BG --confidence 1000 >/dev/null
CTL_CFG="$("$CTL" get --pin-dir "$PINDIR")"
python3 - "$CTL_CFG" "$LAT" "$COMP" "$BG" "$CTL" "$PINDIR" "$BUDGET" "$LAT_SLICE" <<'PY'
import json, subprocess, sys
cfg = json.loads(sys.argv[1])["cfg"]
assert cfg["lat_budget_pct"] == int(sys.argv[7]), cfg
assert cfg["budget_window_ms"] == 100, cfg
assert cfg["lat_slice_ns"] == int(sys.argv[8]), cfg
for pid, klass in ((sys.argv[2], "LAT"), (sys.argv[3], "COMP"), (sys.argv[4], "BG")):
    out = subprocess.check_output([sys.argv[5], "class-get", "--pin-dir", sys.argv[6],
                                   "--pid", pid], text=True)
    got = json.loads(out)
    assert got.get("present") is True and got.get("class") == klass, got
print("[fairness] cfg", cfg["lat_budget_pct"], cfg["budget_window_ms"], cfg["aging_ms"],
      "lat_slice_ns", cfg["lat_slice_ns"])
PY
# The fairness window only lives for budget_window_ms, so a dump taken after
# the probes exit reflects the last dispatch rather than the steady state.
# Sample it while the probes are still running. fair_budget is not pinned by
# the loader, so read it by map name while struct_ops is enabled.
: >"$OUT.budget"
for sample in 1 2 3 4; do
  sleep 0.7
  { echo "sample=$sample"; bpftool map dump name fair_budget 2>&1; } \
    >>"$OUT.budget" || break
done
wait "$LAT" "$COMP" "$NORMAL" "$BG"
PROBES=()
if command -v bpftool >/dev/null 2>&1; then
  bpftool map dump pinned "$PINDIR/stats" >"$OUT.stats" 2>&1 || true
  bpftool map dump pinned "$PINDIR/class_map" >"$OUT.class" 2>&1 || true
  { echo "sample=final"; bpftool map dump name fair_budget 2>&1; } \
    >>"$OUT.budget" || true
fi

python3 - "$TMP/lat.json" "$TMP/comp.json" "$TMP/normal.json" "$TMP/bg.json" "$OUT" "$CPU" \
  "$OUT.stats" "$OUT.budget" "$CTL_CFG" "$BUDGET" <<'PY'
import json, re, sys

# stats/class_map/fair_budget are PERCPU_ARRAY: one key holds one value per CPU.
# Reading a single line of the raw dump understates every counter, so sum the
# CPUs and keep the target-CPU value separately. Indices follow intf.h.
STAT_NAMES = {0: "enq_lat", 5: "dsp_lat", 6: "dsp_comp", 10: "run_lat",
              13: "run_bg", 14: "direct_lat", 20: "class_hit", 21: "class_miss",
              22: "class_invalid", 23: "cache_migrated", 24: "bg_vtime_penalty",
              25: "dispatch_calls", 33: "class_expired",
              34: "lat_budget_throttle", 35: "lat_idle_borrow", 36: "aging_force"}
BUDGET_FIELDS = ("window_start_ns", "lat_runtime_ns", "other_runtime_ns",
                 "last_other_ns")


HEX_LINE = r"(?:[0-9a-f]{2}[ \t]*)+"


def dump_entries(text):
    out = {}
    for block in text.split("key:\n")[1:]:
        lines = [l.strip() for l in block.splitlines() if l.strip()]
        if not lines or not re.fullmatch(HEX_LINE, lines[0]):
            continue
        key = int.from_bytes(bytes.fromhex(re.sub(r"\s", "", lines[0])), "little")
        vals = []
        cur = None
        for line in lines[1:]:
            m = re.match(r"value \(CPU \d+\): ?((?:[0-9a-f]{2}[ \t]*)*)$", line)
            if m:
                if cur is not None:
                    vals.append(cur)
                cur = re.sub(r"\s", "", m.group(1))
            elif cur is not None and re.fullmatch(HEX_LINE, line):
                # Values wider than 8 bytes wrap onto extra lines, and bpftool
                # groups them with double spaces.
                cur += re.sub(r"\s", "", line)
        if cur is not None:
            vals.append(cur)
        if vals:
            out[key] = [bytes.fromhex(v) for v in vals]
    return out


def u64_at(raw, index):
    return int.from_bytes(raw[index * 8:(index + 1) * 8], "little")


rows = {}
for name, path in zip(("LAT", "COMP", "NORMAL", "BG"), sys.argv[1:5]):
    rows[name] = json.load(open(path))
total = sum(r["runtime_ns"] for r in rows.values()) or 1
cpu = int(sys.argv[6])
summary = {"cpu": cpu, "window": rows, "runtime_share_pct": {}}
for name, row in rows.items():
    row["share_pct"] = round(100.0 * row["runtime_ns"] / total, 2)
    summary["runtime_share_pct"][name] = row["share_pct"]

stats = dump_entries(open(sys.argv[7]).read()) if len(sys.argv) > 7 else {}
summary["bpf_stats"] = {
    "per_cpu_sum": {n: sum(int.from_bytes(v, "little") for v in stats.get(i, []))
                    for i, n in STAT_NAMES.items()},
    "target_cpu": {n: (int.from_bytes(stats[i][cpu], "little")
                       if i in stats and len(stats[i]) > cpu else None)
                   for i, n in STAT_NAMES.items()},
    "note": "per-CPU counters summed; single-line reads of the raw dump understate them",
}

budget = []
if len(sys.argv) > 8:
    for chunk in open(sys.argv[8]).read().split("sample=")[1:]:
        label, _, body = chunk.partition("\n")
        entry = dump_entries(body)
        if not entry:
            continue
        for raw in [entry[0][cpu]] if len(entry[0]) > cpu else []:
            sample = {"sample": label.strip(), "raw_bytes": len(raw)}
            for i, field in enumerate(BUDGET_FIELDS):
                sample[field] = u64_at(raw, i)
            lat = sample["lat_runtime_ns"]
            other = sample["other_runtime_ns"]
            sample["lat_share_pct"] = round(100.0 * lat / (lat + other), 2) \
                if lat + other else None
            budget.append(sample)
summary["fair_budget_window_samples"] = budget
summary["fair_budget_note"] = (
    "window covers budget_window_ms; share is accounted runtime, not wall clock")

if len(sys.argv) > 9 and sys.argv[9].strip():
    summary["applied_cfg"] = json.loads(sys.argv[9]).get("cfg")
served = summary["bpf_stats"]["target_cpu"]
per_slice = {}
if served.get("dsp_lat"):
    per_slice["LAT"] = round(rows["LAT"]["runtime_ns"] / served["dsp_lat"] / 1e6, 3)
if served.get("dsp_comp"):
    others_ns = sum(r["runtime_ns"] for n, r in rows.items() if n != "LAT")
    per_slice["OTHERS"] = round(others_ns / served["dsp_comp"] / 1e6, 3)
summary["observed_ms_per_slice"] = per_slice
summary["observed_note"] = (
    "share follows the slice-length quota; lat_budget_pct caps it from above "
    "(throttle fired %s times on the target CPU)" % served.get("lat_budget_throttle"))

json.dump(summary, open(sys.argv[5], "w"), indent=2)
for name, row in rows.items():
    assert row["iters"] > 0, (name, row)
    assert row["runtime_ns"] > 0, (name, row)
    if name != "LAT":
        assert row["share_pct"] >= 5.0, (name, row)
        assert row["max_gap_ns"] <= 500000000, (name, row)
assert rows["LAT"]["share_pct"] <= 80.0, rows["LAT"]
s = summary["bpf_stats"]["per_cpu_sum"]
assert s["class_hit"] > 0, summary["bpf_stats"]
# The reservation has to be both met and capped: throttle=0 would mean the
# budget never bound LAT, which is exactly how the 2026-10-22 first run failed.
budget = int(sys.argv[10])
assert budget - 10 <= rows["LAT"]["share_pct"] <= budget + 10, \
    (budget, rows["LAT"]["share_pct"])
assert s["lat_budget_throttle"] > 0, summary["bpf_stats"]
print(json.dumps(summary, indent=2))
PY

echo "[fairness] summary written to $OUT"
