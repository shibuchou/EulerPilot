#!/usr/bin/env bash
# EulerPilot v2 -- does the suppression actually stop the thrash?
#
# Run 1 of the downtime measurement switched twice in 120s: up when load raised
# PSI past the router threshold, back down seconds after that load disappeared,
# with --auto-min-interval-ms 3000 doing nothing. This replays exactly that load
# profile with dwell and budget enabled and records, second by second, which gate
# is holding the autopilot and when it finally moves.
#
# Expected and asserted:
#   - it still switches up (suppression must not disable the feature);
#   - the gate named by EXPECT_GATE is the one that holds the flip back, and the
#     executor's own accounting shows it blocked right up to its threshold;
#   - total switches in the run stay within the budget.
#
# EXPECT_GATE=dwell (default) replays run 1's profile as-is. EXPECT_GATE=candidate_hold
# and EXPECT_GATE=budget are run with the *other* gates at the CLI floor of 1000ms --
# 0 is refused by --auto-dwell-ms/--auto-candidate-hold-ms (they accept 1000..3600000),
# so the executor would never start -- so the only thing that can delay or refuse the
# switch is the gate under test.
#
# Needs root, an idle sched_ext, and a prior cmake build. Manual, not part of ctest.
set -uo pipefail

EXECUTORD="${1:?usage: auto_apply_suppression_sp3.sh <ep-executord> <ep> <ep-sched-loader> [out-dir]}"
EP="${2:?usage: auto_apply_suppression_sp3.sh <ep-executord> <ep> <ep-sched-loader> [out-dir]}"
LOADER="${3:?usage: auto_apply_suppression_sp3.sh <ep-executord> <ep> <ep-sched-loader> [out-dir]}"
OUT="${4:-/root/epv2-evidence/auto-suppress}"
CTL="$(dirname "$LOADER")/ep-schedctl"
PIN_ROOT=/sys/fs/bpf/eulerpilot-v2
DWELL_MS="${DWELL_MS:-60000}"
CANDIDATE_HOLD_MS="${CANDIDATE_HOLD_MS:-15000}"
BUDGET_WINDOW_MS="${BUDGET_WINDOW_MS:-600000}"
BUDGET_MAX="${BUDGET_MAX:-2}"
OBSERVE_SECONDS="${OBSERVE_SECONDS:-100}"
UP_WAIT="${UP_WAIT:-120}"
# Which single gate this run is meant to make binding. The caller loosens the
# other two so the timeline can only be explained by the gate under test.
EXPECT_GATE="${EXPECT_GATE:-dwell}"
case "$EXPECT_GATE" in
  dwell)          gate_threshold="$DWELL_MS";          gate_pattern='not met (in place [0-9]*ms)' ;;
  candidate_hold) gate_threshold="$CANDIDATE_HOLD_MS"; gate_pattern='routing target stable [0-9]*ms' ;;
  budget)         gate_threshold=0;                    gate_pattern='switch budget [0-9]*/' ;;
  *) echo "unknown EXPECT_GATE=$EXPECT_GATE"; exit 77 ;;
esac
RUN=/tmp/ep-suppress

[ "$(id -u)" = 0 ] || { echo "requires root"; exit 77; }
[ "$(cat /sys/kernel/sched_ext/state 2>/dev/null)" = "disabled" ] || { echo "sched_ext not idle"; exit 1; }
[ -z "$(pgrep -x ep-executord)" ] || { echo "executor already running"; exit 1; }
[ -z "$(pgrep -x schedpilotd)" ] || { echo "schedpilotd is running"; exit 1; }

mkdir -p "$OUT"
rm -rf "$RUN"; mkdir -p "$RUN/state"
SOCK="$RUN/executor.sock"; BASELINE="$RUN/baseline.json"
DAEMON=""; LOAD_PIDS=()

cleanup() {
  local rc=$?
  for pid in "${LOAD_PIDS[@]:-}"; do kill -KILL "$pid" 2>/dev/null; wait "$pid" 2>/dev/null; done
  if [ -n "$DAEMON" ] && kill -0 "$DAEMON" 2>/dev/null; then
    kill -TERM "$DAEMON" 2>/dev/null; wait "$DAEMON" 2>/dev/null
  fi
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

python3 - "$BASELINE" <<'PY'
import json, sys
json.dump({
  "apiVersion": "eulerpilot.io/v1alpha1", "kind": "SchedulingPolicy",
  "metadata": {"name": "suppress-probe"},
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
               "policy_ttl_ms": 900000, "quiet_max_regression_pct": 5,
               "evaluation_min_bg_progress_ratio": 0.5, "bg_work_unit": "requests",
               "bg_progress_window_ms": 5000, "min_bg_work_units_per_sec": 1,
               "max_bg_wait_ms": 1000, "fallback": "verified-safe-profile",
               "allow_affinity_change": False}
  }
}, open(sys.argv[1], "w"))
PY

"$EXECUTORD" --state-dir "$RUN/state" --socket "$SOCK" --skill sched \
  --loader "$LOADER" --sched-ctl "$CTL" \
  --auto-apply --auto-policy "$BASELINE" --auto-owner autopilot-v2 \
  --auto-min-interval-ms 3000 --auto-lease-ttl-ms 900000 \
  --auto-dwell-ms "$DWELL_MS" --auto-candidate-hold-ms "$CANDIDATE_HOLD_MS" \
  --auto-budget-window-ms "$BUDGET_WINDOW_MS" --auto-budget-max-switches "$BUDGET_MAX" \
  >"$RUN/daemon.log" 2>&1 &
DAEMON=$!
ready=""
for _ in $(seq 1 100); do
  if "$EP" status --socket "$SOCK" 2>/dev/null | grep -q '"auto_apply"'; then ready=1; break; fi
  sleep 0.1
done
if [ -z "$ready" ]; then
  # Two runs were lost to a silently empty timeline: an argument the executor
  # refuses means no socket, not a suppression result. Fail loudly instead.
  echo "FATAL executor never answered on $SOCK"
  cat "$RUN/daemon.log"
  exit 1
fi

"$EP" apply --file "$BASELINE" --idempotency-key sup-apply-1 --socket "$SOCK" >"$OUT/apply.json" 2>&1
"$EP" lease --owner autopilot-v2 --ttl-ms 900000 --socket "$SOCK" >"$OUT/handover.json" 2>&1

for _ in $(seq 1 $(( $(nproc) * 2 ))); do
  (while :; do :; done) </dev/null >/dev/null 2>&1 & LOAD_PIDS+=("$!")
done
echo "load raised; waiting for the switch up"

switch_at=""
record() {  # elapsed_s, phase
  st=$("$EP" status --socket "$SOCK")
  expert=$(echo "$st" | python3 -c "import json,sys;print(json.load(sys.stdin)['auto_apply']['active_expert'])")
  held=$(echo "$st" | python3 -c "import json,sys;d=json.load(sys.stdin)['auto_apply'];print(d['last_hold'] or d['last_reason'])")
  cnt=$(echo "$st" | python3 -c "import json,sys;print(json.load(sys.stdin)['auto_apply']['switches_recorded'])")
  echo "$1,$2,$expert,$cnt,\"$held\"" >> "$OUT/timeline.csv"
  echo "$expert"
}

: > "$OUT/timeline.csv"
echo "elapsed,phase,active_expert,switches,gate" >> "$OUT/timeline.csv"

# Phase 1: load is already up. The first switch can only happen after dwell has
# elapsed since the human apply, so the wait has to cover that.
up=""
for n in $(seq 1 "$UP_WAIT"); do
  sleep 1
  expert=$(record "$n" waiting_up)
  if [ "$expert" = "latency-balanced" ]; then up=$n; echo "  switched up at t=${n}s"; break; fi
done
if [ -z "$up" ]; then
  echo "RESULT: never switched up within ${UP_WAIT}s - suppression too strict (see timeline.csv)"
  grep -c "autopilot switched" "$RUN/daemon.log" | sed 's/^/  switches_in_log=/'
  tail -3 "$OUT/timeline.csv"
  exit 1
fi

for pid in "${LOAD_PIDS[@]:-}"; do kill -KILL "$pid" 2>/dev/null; wait "$pid" 2>/dev/null; done
LOAD_PIDS=()
echo "load dropped; run1 switched back within seconds, this must now wait for dwell"

returned=""
for n in $(seq 1 "$OBSERVE_SECONDS"); do
  sleep 1
  expert=$(record "$n" after_drop)
  if [ -z "$returned" ] && [ "$expert" = "safe-basic" ]; then
    returned=$n
    echo "  returned to safe-basic ${n}s after the drop (dwell was ${DWELL_MS} ms)"
  fi
  [ $((n % 15)) -eq 0 ] && echo "  t=+${n}s expert=$expert"
done

sw_total=$(grep -c "autopilot switched" "$RUN/daemon.log")
echo "RESULT gate=$EXPECT_GATE switches=$sw_total returned_at_s=${returned:-none} dwell_ms=$DWELL_MS candidate_hold_ms=$CANDIDATE_HOLD_MS budget=${BUDGET_MAX}/${BUDGET_WINDOW_MS}ms"
if [ "$EXPECT_GATE" = dwell ]; then
  thirty=$(awk -F, '$2=="after_drop" && $1>=25 && $1<=35 {print $3; exit}' "$OUT/timeline.csv")
  if [ "$thirty" = "latency-balanced" ]; then
    echo "CHECK_HELD_AT_30S=yes"
  else
    echo "CHECK_HELD_AT_30S=no (expert at 30s: $thirty)"
  fi
fi
if [ "$sw_total" -le "$BUDGET_MAX" ]; then echo "CHECK_WITHIN_BUDGET=yes"; else echo "CHECK_WITHIN_BUDGET=no"; fi
# Judge suppression from the executor's own accounting in the timeline, not from
# wall-clock subtraction here: every suppression gate text carries the number it
# compared against, and the largest one before a flip is exactly what we want.
bound=$(grep -c "$gate_pattern" "$OUT/timeline.csv")
peak=$(grep -o "$gate_pattern" "$OUT/timeline.csv" | grep -o '[0-9]\+' | sort -n | tail -1)
peak=${peak:-0}
floor=$(( gate_threshold - 4000 ))
if [ "$bound" -eq 0 ]; then
  echo "CHECK_GATE_${EXPECT_GATE}=never bound (no suppression row in timeline)"
elif [ "$EXPECT_GATE" = budget ]; then
  echo "CHECK_GATE_budget=bound $bound times, switches stayed $sw_total of $BUDGET_MAX"
elif [ "$peak" -ge "$floor" ]; then
  echo "CHECK_GATE_${EXPECT_GATE}=honored (last blocked ${peak}ms of ${gate_threshold}ms, $bound rows)"
else
  echo "CHECK_GATE_${EXPECT_GATE}=violated (last blocked only ${peak}ms of ${gate_threshold}ms)"
fi
echo "GATE_CENSUS dwell=$(grep -c 'not met (in place' "$OUT/timeline.csv") candidate=$(grep -c 'routing target stable' "$OUT/timeline.csv") budget=$(grep -c 'switch budget' "$OUT/timeline.csv")"
cp "$RUN/daemon.log" "$OUT/daemon.log"
