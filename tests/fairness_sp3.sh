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
if [ "$BUDGET" != "60" ]; then
  "$CTL" set --pin-dir "$PINDIR" --json "{\"lat_budget_pct\":$BUDGET}" >/dev/null
fi
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
python3 - "$CTL_CFG" "$LAT" "$COMP" "$BG" "$CTL" "$PINDIR" "$BUDGET" <<'PY'
import json, subprocess, sys
cfg = json.loads(sys.argv[1])["cfg"]
assert cfg["lat_budget_pct"] == int(sys.argv[7]), cfg
assert cfg["budget_window_ms"] == 100, cfg
for pid, klass in ((sys.argv[2], "LAT"), (sys.argv[3], "COMP"), (sys.argv[4], "BG")):
    out = subprocess.check_output([sys.argv[5], "class-get", "--pin-dir", sys.argv[6],
                                   "--pid", pid], text=True)
    got = json.loads(out)
    assert got.get("present") is True and got.get("class") == klass, got
print("[fairness] cfg", cfg["lat_budget_pct"], cfg["budget_window_ms"], cfg["aging_ms"])
PY
wait "$LAT" "$COMP" "$NORMAL" "$BG"
PROBES=()
if command -v bpftool >/dev/null 2>&1; then
  bpftool map dump pinned "$PINDIR/stats" >"$OUT.stats" 2>&1 || true
  bpftool map dump pinned "$PINDIR/class_map" >"$OUT.class" 2>&1 || true
fi

python3 - "$TMP/lat.json" "$TMP/comp.json" "$TMP/normal.json" "$TMP/bg.json" "$OUT" "$CPU" <<'PY'
import json, sys
rows = {}
for name, path in zip(("LAT", "COMP", "NORMAL", "BG"), sys.argv[1:5]):
    rows[name] = json.load(open(path))
total = sum(r["runtime_ns"] for r in rows.values()) or 1
summary = {"cpu": int(sys.argv[6]), "window": rows, "runtime_share_pct": {}}
for name, row in rows.items():
    row["share_pct"] = round(100.0 * row["runtime_ns"] / total, 2)
    summary["runtime_share_pct"][name] = row["share_pct"]
json.dump(summary, open(sys.argv[5], "w"), indent=2)
for name, row in rows.items():
    assert row["iters"] > 0, (name, row)
    assert row["runtime_ns"] > 0, (name, row)
    if name != "LAT":
        assert row["share_pct"] >= 5.0, (name, row)
        assert row["max_gap_ns"] <= 500000000, (name, row)
assert rows["LAT"]["share_pct"] <= 80.0, rows["LAT"]
print(json.dumps(summary, indent=2))
PY

echo "[fairness] summary written to $OUT"
