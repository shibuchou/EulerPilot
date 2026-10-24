#!/usr/bin/env bash
# EulerPilot v2 -- how much of the service signal is noise?
#
# The SLO gate compares one measurement window with another and the policy
# declares a 5% forward-regression budget. The first SP3 run of that gate produced
# same-state window-to-window p99 swings of ~46%, so the gate was refusing experts
# on noise. Before choosing a budget or a window length, measure the metric itself.
#
# This characterises the *measurement*, not the control loop: it never touches
# sched_ext, the executor or the scheduler. At a constant offered load it repeats
# foreground/background windows of three lengths, one pair at a time and running
# together so both see the same world, and reports dispersion per (state, window).
#
# Needs root only to keep the spinner accounting consistent with the other
# experiments. Manual, not part of ctest.
set -uo pipefail

PROBE="${PROBE:-$(dirname "$0")/service_load_probe}"
OUT="${1:-/root/epv2-evidence/slo-noise}"
REPS="${REPS:-5}"
WINDOWS="${WINDOWS:-5 15 30}"
WARMUP_S="${WARMUP_S:-10}"
EXPERT="${EXPERT:-safe-basic}"
LOAD_PIDS=()

[ -x "$PROBE" ] || { echo "missing probe: $PROBE (compile experiments/service_load_probe.c)"; exit 1; }
mkdir -p "$OUT"
: > "$OUT/noise.tsv"
printf "state\trep\twindow_s\tp99_ms\tbg_units\tbg_wait_ms\tfg_samples\tbg_samples\n" >> "$OUT/noise.tsv"

stop_load() {
  for pid in "${LOAD_PIDS[@]:-}"; do kill -KILL "$pid" 2>/dev/null; wait "$pid" 2>/dev/null; done
  LOAD_PIDS=()
}
cleanup() {
  local rc=$?
  stop_load
  echo "cleanup rc=$rc"
}
trap cleanup EXIT

pair() {  # state rep window
  local state=$1 rep=$2 w=$3
  # Both roles run at the same time so the pair describes one world; the shell
  # cannot capture a backgrounded command substitution, so each writes a file.
  "$PROBE" fg "$EXPERT" "$w" 1000 > "$OUT/fg.tmp" 2>/dev/null &
  local job=$!
  "$PROBE" bg "$EXPERT" "$w" > "$OUT/bg.tmp" 2>/dev/null
  wait "$job"
  python3 - "$state" "$rep" "$w" "$OUT/fg.tmp" "$OUT/bg.tmp" >> "$OUT/noise.tsv" <<'PY'
import json, sys
state, rep, w = sys.argv[1], sys.argv[2], sys.argv[3]
fg = json.loads(open(sys.argv[4]).read())
bg = json.loads(open(sys.argv[5]).read())
print("%s\t%s\t%s\t%.6f\t%.3f\t%.3f\t%d\t%d" % (
    state, rep, w, fg["p99_latency_ms"], bg["work_units_per_sec"], bg["max_wait_ms"],
    fg["samples"], bg["samples"]))
PY
}

state_run() {  # state
  local state=$1 rep w
  if [ "$state" = "loaded" ]; then
    for _ in $(seq 1 $(( $(nproc) * 2 ))); do
      (while :; do :; done) </dev/null >/dev/null 2>&1 & LOAD_PIDS+=("$!")
    done
  fi
  echo "### $state: warming up ${WARMUP_S}s"
  sleep "$WARMUP_S"
  for rep in $(seq 1 "$REPS"); do
    for w in $WINDOWS; do
      pair "$state" "$rep" "$w" || { echo "FATAL pair failed ($state rep=$rep w=$w)"; exit 1; }
      echo "  $state rep=$rep window=${w}s done $(date +%T)"
    done
  done
  stop_load
  sleep 5
}

state_run idle
state_run loaded

python3 - "$OUT/noise.tsv" <<'PY'
import csv, statistics, sys
rows = [r for r in csv.DictReader(open(sys.argv[1]), delimiter="\t")]
print("rep 1 excluded as warm-up per state")
print("state\twindow_s\tn\tp99 mean\tp99 min..max\tp99 CV\tconsecutive |delta|\tverdict at the declared 5% budget\tbg max |delta|")
for state in ("idle", "loaded"):
    for w in sorted({int(r["window_s"]) for r in rows}):
        sel = [r for r in rows if r["state"] == state and int(r["window_s"]) == w and int(r["rep"]) > 1]
        if len(sel) < 2:
            continue
        p99 = [float(r["p99_ms"]) for r in sel]
        mean = statistics.mean(p99)
        cv = 100 * statistics.pstdev(p99) / mean if mean else 0
        deltas = [abs(p99[i] - p99[i - 1]) / p99[i - 1] * 100 for i in range(1, len(p99)) if p99[i - 1]]
        bgv = [float(r["bg_units"]) for r in sel]
        bg_deltas = [abs(bgv[i] - bgv[i - 1]) / bgv[i - 1] * 100 for i in range(1, len(bgv)) if bgv[i - 1]]
        worst = max(deltas) if deltas else 0
        print("%s\t%d\t%d\t%.4f ms\t%.4f..%.4f\t%.1f%%\tmedian %.1f%% / max %.1f%%\t%s\t%.1f%%" % (
            state, w, len(sel), mean, min(p99), max(p99), cv,
            statistics.median(deltas) if deltas else 0, worst,
            "usable" if worst <= 5 else "NOT usable",
            max(bg_deltas) if bg_deltas else 0))
PY
