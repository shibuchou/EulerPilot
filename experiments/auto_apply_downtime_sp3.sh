#!/usr/bin/env bash
# EulerPilot v2 -- quantify the cost of an automatic policy switch.
#
# Two things get measured, both in CLOCK_MONOTONIC so no clock alignment is needed:
#   downtime   how long sched_ext sits without OUR scheduler while a switch runs
#              (rollback detaches, apply re-attaches: the protection gap is real)
#   jitters    periodic wake-up lateness before, during and after that gap
#
# A switch is produced the way it happens in practice: the autopilot holds the
# lease, load pushes PSI past the router threshold, and it moves safe-basic to
# latency-balanced on its own. The first attach (human apply from an idle host)
# is measured too, because it shares the same detach/attach shape.
#
# Run manually on an idle SP3 with root. Not part of ctest.
set -uo pipefail

EXECUTORD="${1:?usage: auto_apply_downtime_sp3.sh <ep-executord> <ep> <ep-sched-loader> [out-dir]}"
EP="${2:?usage: auto_apply_downtime_sp3.sh <ep-executord> <ep> <ep-sched-loader> [out-dir]}"
LOADER="${3:?usage: auto_apply_downtime_sp3.sh <ep-executord> <ep> <ep-sched-loader> [out-dir]}"
OUT="${4:-/root/epv2-evidence/auto-downtime}"
CTL="$(dirname "$LOADER")/ep-schedctl"
HERE="$(cd "$(dirname "$0")" && pwd)"
PROBE_SRC="$HERE/sched_switch_probe.c"
PIN_ROOT=/sys/fs/bpf/eulerpilot-v2
PROBES=4
PROBE_INTERVAL_US=1000
PROBE_SECONDS=120
RUN=/tmp/ep-downtime

[ "$(id -u)" = 0 ] || { echo "requires root"; exit 77; }
[ "$(cat /sys/kernel/sched_ext/state 2>/dev/null)" = "disabled" ] || { echo "sched_ext not idle"; exit 1; }
[ -z "$(pgrep -x ep-executord)" ] || { echo "executor already running"; exit 1; }
[ -z "$(pgrep -x schedpilotd)" ] || { echo "schedpilotd is running"; exit 1; }

mkdir -p "$OUT"
rm -rf "$RUN"; mkdir -p "$RUN/state"
SOCK="$RUN/executor.sock"; BASELINE="$RUN/baseline.json"; PROBE_BIN="$RUN/probe"
DAEMON=""; WATCH=""; LOAD_PIDS=(); PROBE_PIDS=()

cleanup() {
  local rc=$?
  for pid in "${PROBE_PIDS[@]:-}" "${LOAD_PIDS[@]:-}"; do kill -KILL "$pid" 2>/dev/null; done
  [ -n "$WATCH" ] && kill -TERM "$WATCH" 2>/dev/null
  if [ -n "$DAEMON" ] && kill -0 "$DAEMON" 2>/dev/null; then kill -TERM "$DAEMON" 2>/dev/null; fi
  wait 2>/dev/null
  for _ in $(seq 1 60); do
    [ "$(cat /sys/kernel/sched_ext/state 2>/dev/null)" = "disabled" ] && break
    sleep 0.5
  done
  local inst
  inst=$(python3 -c "import json;print(json.load(open('$RUN/state/state.json'))['instance_id'])" 2>/dev/null)
  if [ -n "${inst:-}" ] && [ -d "$PIN_ROOT/$inst" ] &&
     [ "$(cat /sys/kernel/sched_ext/state 2>/dev/null)" = "disabled" ]; then
    rm -rf "$PIN_ROOT/$inst"
  fi
  rmdir "$PIN_ROOT" 2>/dev/null
  echo "cleanup rc=$rc state=$(cat /sys/kernel/sched_ext/state 2>/dev/null) pins=$(ls -1 "$PIN_ROOT" 2>/dev/null|wc -l)"
}
trap cleanup EXIT

gcc -O2 -o "$PROBE_BIN" "$PROBE_SRC" || { echo "probe build failed"; exit 1; }

python3 - "$BASELINE" <<'PY'
import json, sys
json.dump({
  "apiVersion": "eulerpilot.io/v1alpha1", "kind": "SchedulingPolicy",
  "metadata": {"name": "downtime-probe"},
  "spec": {
    "scope": "scheduler-instance", "mode": "class", "expert": "safe-basic",
    "source": "human",
    "targets": [{"kind": "pid", "pid": 1, "role": "FOREGROUND", "role_source": "explicit"}],
    "parameters": {"lat_slice_ns": 1000000, "comp_slice_ns": 4000000,
                   "cache_slice_ns": 4000000, "bg_slice_ns": 2000000,
                   "preempt_threshold_ns": 500000, "bg_vtime_pct": 200,
                   "llc_affinity": False},
    "safety": {"min_dwell_ms": 5000, "max_observation_age_ms": 1000,
               "heartbeat_period_ms": 250, "heartbeat_timeout_ms": 2000,
               "policy_ttl_ms": 600000, "quiet_max_regression_pct": 5,
               "evaluation_min_bg_progress_ratio": 0.5, "bg_work_unit": "requests",
               "bg_progress_window_ms": 5000, "min_bg_work_units_per_sec": 1,
               "max_bg_wait_ms": 1000, "fallback": "verified-safe-profile",
               "allow_affinity_change": False}
  }
}, open(sys.argv[1], "w"))
PY

# Every background process must release the inherited stdout, otherwise an
# interactive session never sees EOF and a bare wait blocks on them forever.
"$PROBE_BIN" watch "$OUT/watch.tsv" "$PROBE_SECONDS" </dev/null >/dev/null 2>&1 &
WATCH=$!
for i in $(seq 1 "$PROBES"); do
  "$PROBE_BIN" probe "$OUT/probe$i.bin" "$PROBE_INTERVAL_US" "$PROBE_SECONDS"\
    </dev/null >/dev/null 2>&1 &
  PROBE_PIDS+=("$!")
done
sleep 1
BASE_START=$(python3 -c "import time;print(int(time.clock_gettime(time.CLOCK_MONOTONIC)*1e9))")
echo "baseline segment: 6s with no v2 scheduler attached"
sleep 6

"$EXECUTORD" --state-dir "$RUN/state" --socket "$SOCK" --skill sched \
  --loader "$LOADER" --sched-ctl "$CTL" \
  --auto-apply --auto-policy "$BASELINE" --auto-owner autopilot-v2 \
  --auto-min-interval-ms 3000 --auto-lease-ttl-ms 600000 >"$RUN/daemon.log" 2>&1 &
DAEMON=$!
for _ in $(seq 1 100); do "$EP" status --socket "$SOCK" >/dev/null 2>&1 && break; sleep 0.1; done
echo "human apply (attach #1)"
"$EP" apply --file "$BASELINE" --idempotency-key dt-apply-1 --socket "$SOCK" >"$OUT/apply1.json" 2>&1
sleep 6
"$EP" status --socket "$SOCK" > "$OUT/status-after-apply.json"

# Hand over, then push PSI past the router threshold so the switch is automatic.
"$EP" lease --owner autopilot-v2 --ttl-ms 600000 --socket "$SOCK" >"$OUT/handover.json" 2>&1
for _ in $(seq 1 $(( $(nproc) * 2 ))); do
  (while :; do :; done) </dev/null >/dev/null 2>&1 & LOAD_PIDS+=("$!")
done
echo "load raised to trigger the routed switch; waiting"
for _ in $(seq 1 40); do
  sleep 1
  exp=$(python3 -c "import json;print(json.load(open('/dev/stdin'))['auto_apply']['active_expert'])" \
        < <("$EP" status --socket "$SOCK"))
  [ "$exp" != "safe-basic" ] && { echo "switched to $exp"; break; }
done
sleep 6
for pid in "${LOAD_PIDS[@]:-}"; do kill -KILL "$pid" 2>/dev/null; wait "$pid" 2>/dev/null; done
LOAD_PIDS=()

echo "stopping probes; the final detach happens in cleanup when the executor exits"
"$EP" status --socket "$SOCK" > "$OUT/status-after-switch.json"
for i in $(seq 1 "$PROBES"); do kill -TERM "${PROBE_PIDS[$((i-1))]}" 2>/dev/null; done
for pid in "${PROBE_PIDS[@]:-}"; do [ -n "$pid" ] && wait "$pid" 2>/dev/null; done
kill -TERM "$WATCH" 2>/dev/null; wait "$WATCH" 2>/dev/null; WATCH=""
BASE_END=$(python3 -c "import time;print(int(time.clock_gettime(time.CLOCK_MONOTONIC)*1e9))")
echo "BASELINE_START=$BASE_START BASELINE_END=$BASE_END"
# Analysis lives in analyze_switch_downtime.py so the same segmentation can be
# re-run against the raw files without repeating the experiment.
python3 "$HERE/analyze_switch_downtime.py" "$OUT" "$OUT/summary.json" | tail -25
