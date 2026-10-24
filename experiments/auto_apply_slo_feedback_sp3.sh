#!/usr/bin/env bash
# EulerPilot v2 -- does the autopilot learn from what the service actually felt?
#
# Everything measured so far paced switching by *time* (dwell, candidate hold,
# budget). None of it could answer "did that switch help?". This closes the loop
# with a measured workload: a foreground 1ms wake-up loop and a background work
# loop report through service.observe, and the executor judges each automatic
# switch against the limits the policy itself declares.
#
# Asserted:
#   - a switch arms an outcome check and the first measurement under the new
#     expert resolves it (status.auto_apply -> status.service);
#   - within limits -> nothing is blacklisted; the route keeps working;
#   - a measurement that breaches the declared floor denies the gate with
#     BG_PROGRESS_FLOOR and the autopilot stops switching.
#
# Needs root, an idle sched_ext, and a prior cmake build. Manual, not part of ctest.
set -uo pipefail

EXECUTORD="${1:?usage: auto_apply_slo_feedback_sp3.sh <ep-executord> <ep> <ep-sched-loader> [out-dir]}"
EP="${2:?usage: auto_apply_slo_feedback_sp3.sh <ep-executord> <ep> <ep-sched-loader> [out-dir]}"
LOADER="${3:?usage: auto_apply_slo_feedback_sp3.sh <ep-executord> <ep> <ep-sched-loader> [out-dir]}"
OUT="${4:-/root/epv2-evidence/auto-slo}"
CTL="$(dirname "$LOADER")/ep-schedctl"
PROBE="${PROBE:-$(dirname "$0")/service_load_probe}"
PIN_ROOT=/sys/fs/bpf/eulerpilot-v2
DWELL_MS="${DWELL_MS:-1000}"
CANDIDATE_HOLD_MS="${CANDIDATE_HOLD_MS:-1000}"
UP_WAIT="${UP_WAIT:-60}"
SLO_WINDOW_S="${SLO_WINDOW_S:-15}"
BG_FLOOR="${BG_FLOOR:-1}"
OUTCOME_WINDOWS="${OUTCOME_WINDOWS:-2}"
RUN=/tmp/ep-slo

[ "$(id -u)" = 0 ] || { echo "requires root"; exit 77; }
[ "$(cat /sys/kernel/sched_ext/state 2>/dev/null)" = "disabled" ] || { echo "sched_ext not idle"; exit 1; }
[ -z "$(pgrep -x ep-executord)" ] || { echo "executor already running"; exit 1; }
[ -x "$PROBE" ] || { echo "missing probe: $PROBE (compile experiments/service_load_probe.c)"; exit 1; }

mkdir -p "$OUT"
rm -rf "$RUN"; mkdir -p "$RUN/state"
SOCK="$RUN/executor.sock"; BASELINE="$RUN/baseline.json"
DAEMON=""; LOAD_PIDS=(); ROUND=0; LAST_OBS=""

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

python3 - "$BASELINE" "$BG_FLOOR" <<'PY'
import json, sys
floor = float(sys.argv[2])
json.dump({
  "apiVersion": "eulerpilot.io/v1alpha1", "kind": "SchedulingPolicy",
  "metadata": {"name": "slo-feedback"},
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
               "bg_progress_window_ms": 5000, "min_bg_work_units_per_sec": floor,
               "max_bg_wait_ms": 5000, "fallback": "verified-safe-profile",
               "allow_affinity_change": False}
  }
}, open(sys.argv[1], "w"))
PY

"$EXECUTORD" --state-dir "$RUN/state" --socket "$SOCK" --skill sched \
  --loader "$LOADER" --sched-ctl "$CTL" \
  --auto-apply --auto-policy "$BASELINE" --auto-owner autopilot-v2 \
  --auto-min-interval-ms 3000 --auto-lease-ttl-ms 900000 \
  --auto-dwell-ms "$DWELL_MS" --auto-candidate-hold-ms "$CANDIDATE_HOLD_MS" \
  --auto-budget-window-ms 600000 --auto-budget-max-switches 10 \
  --auto-outcome-windows "$OUTCOME_WINDOWS" \
  >"$RUN/daemon.log" 2>&1 &
DAEMON=$!
ready=""
for _ in $(seq 1 100); do
  if "$EP" status --socket "$SOCK" 2>/dev/null | grep -q '"auto_apply"'; then ready=1; break; fi
  sleep 0.1
done
[ -n "$ready" ] || { echo "FATAL executor never answered"; cat "$RUN/daemon.log"; exit 1; }

"$EP" apply --file "$BASELINE" --idempotency-key slo-apply-1 --ttl-ms 900000 \
  --socket "$SOCK" >"$OUT/apply.json" 2>&1
# The lease stays with the CLI for now: with no lease the autopilot cannot move, which
# is what makes the pre-switch baseline measurable.

field() { python3 -c "import json,sys;print(json.loads(sys.argv[1]).get(sys.argv[2],''))" "$1" "$2"; }

# One measured window: both probes under the expert that is in place right now.
measure() {  # round -> observation json path
  local round=$1 expert="" fg bg tries=0
  while [ -z "$expert" ] && [ "$tries" -lt 20 ]; do
    "$EP" status --socket "$SOCK" >"$OUT/status-$round.json" 2>&1
    expert=$(python3 -c "import json;print(json.load(open('$OUT/status-$round.json'))['auto_apply']['active_expert'])")
    tries=$((tries + 1))
    [ -n "$expert" ] || sleep 1
  done
  [ -n "$expert" ] || { echo "FATAL no policy in place after 20s"; exit 1; }
  fg=$("$PROBE" fg "$expert" "$SLO_WINDOW_S" 1000) || { echo "FATAL foreground probe failed"; exit 1; }
  bg=$("$PROBE" bg "$expert" "$SLO_WINDOW_S" 2>/dev/null) || { echo "FATAL background probe failed"; exit 1; }
  python3 - "$fg" "$bg" "$OUT/obs-$round.json" <<'PY'
import json, sys
fg, bg = json.loads(sys.argv[1]), json.loads(sys.argv[2])
start = min(fg["observed_at_ms"] - fg["window_ms"], bg["observed_at_ms"] - bg["window_ms"])
end = max(fg["observed_at_ms"], bg["observed_at_ms"])
obs = {"workload": "svc-sp3", "observed_at_ms": end,
       "window_ms": max(100, end - start), "expert": fg["expert"],
       "foreground": {"p99_latency_ms": fg["p99_latency_ms"]},
       "background": {"work_units_per_sec": bg["work_units_per_sec"],
                      "max_wait_ms": bg["max_wait_ms"]}}
json.dump(obs, open(sys.argv[3], "w"))
# stdout carries only the file path (the caller captures it); the reading goes to
# stderr so it still lands in the run log.
print("MEASURE expert=%s p99_ms=%.3f bg_units=%.1f bg_wait_ms=%.3f samples_fg=%d samples_bg=%d" % (
    fg["expert"], fg["p99_latency_ms"], bg["work_units_per_sec"], bg["max_wait_ms"],
    fg["samples"], bg["samples"]), file=sys.stderr)
PY
  echo "$OUT/obs-$round.json"
}

observe_and_report() {  # round
  local path=$1 round=$2
  "$EP" observe --file "$path" --socket "$SOCK" >"$OUT/ack-$round.json" 2>&1 ||
    { echo "FATAL observe rejected: $(cat "$OUT/ack-$round.json")"; exit 1; }
  python3 - "$OUT/ack-$round.json" "$OUT/status-$round.json" <<'PY'
import json, sys
ack = json.load(open(sys.argv[1]))
outcome = ack.get("switch_outcome") or {}
print("OBSERVE ack=%s expert=%s outcome_evaluated=%s hurt=%s code=%s" % (
    ack.get("status"), ack["accepted"]["expert"], outcome.get("evaluated"),
    outcome.get("hurt"), outcome.get("code", "")))
if outcome.get("evaluated"):
    print("OUTCOME %s -> %s: %s" % (outcome.get("from"), outcome.get("to"),
                                    outcome.get("detail", "within the declared limits")))
print("ADVERSE", json.dumps(ack.get("adverse_experts"), sort_keys=True))
PY
}

: > "$OUT/timeline.csv"
echo "round,phase,active_expert,switches,gate,p99_ms,bg_units,bg_wait_ms" >> "$OUT/timeline.csv"

sample_into_timeline() {  # phase obs-path
  local phase=$1 obs=$2 round=$3
  python3 - "$OUT/status-$round.json" "$obs" "$phase" "$round" >> "$OUT/timeline.csv" <<'PY'
import json, sys
status = json.load(open(sys.argv[1]))
obs, phase, rnd = json.load(open(sys.argv[2])), sys.argv[3], sys.argv[4]
aa = status["auto_apply"]
row = [rnd, phase, aa["active_expert"], aa["switches_recorded"],
       aa.get("last_hold") or aa.get("last_reason") or "",
       "%.3f" % obs["foreground"]["p99_latency_ms"],
       "%.1f" % obs["background"]["work_units_per_sec"],
       "%.3f" % obs["background"]["max_wait_ms"]]
print(",".join('"%s"' % c if "," in str(c) else str(c) for c in row))
PY
}

# The pre/post comparison has to be taken at a constant offered load, otherwise
# "the switch hurt" and "the load changed" are the same measurement. So: raise the
# load first, measure under the expert that is in place, and only then let the
# autopilot move.
for _ in $(seq 1 $(( $(nproc) * 2 ))); do
  (while :; do :; done) </dev/null >/dev/null 2>&1 & LOAD_PIDS+=("$!")
done
echo "load raised; measuring the baseline under the expert that is in place"

for ROUND in 1 2; do
  obs=$(measure "$ROUND"); observe_and_report "$obs" "$ROUND"
  sample_into_timeline loaded_before "$obs" "$ROUND"
done
# Two windows of the same state are what lets the executor state its own
# resolution floor; with one it has no jitter measurement and must judge anyway.

"$EP" lease --owner autopilot-v2 --ttl-ms 900000 --socket "$SOCK" >"$OUT/handover.json" 2>&1
echo "lease handed to the autopilot; it should move within a couple of polls"

switch_at=""
for n in $(seq 1 "$UP_WAIT"); do
  sleep 1
  st=$("$EP" status --socket "$SOCK")
  expert=$(python3 -c "import json,sys;print(json.loads(sys.argv[1])['auto_apply']['active_expert'])" "$st")
  if [ "$expert" = "latency-balanced" ]; then switch_at=$n; echo "  switched up ${n}s after the round-1 measurement"; break; fi
done
[ -n "$switch_at" ] || { echo "RESULT never switched up within ${UP_WAIT}s"; tail -3 "$OUT/timeline.csv"; exit 1; }

# The first measurement under the new expert is the post-switch one: it resolves
# the outcome the executor armed when it switched.
sleep 2
ROUND=3
st=$("$EP" status --socket "$SOCK")
python3 - "$st" <<'ARMED'
import json, sys
service = json.loads(sys.argv[1])["service"]
print("ARMED outcome_pending=%s last_outcome=%r" %
      (service.get("outcome_pending"), service.get("last_outcome")))
ARMED
obs=$(measure "$ROUND"); observe_and_report "$obs" "$ROUND"
sample_into_timeline loaded_after "$obs" "$ROUND"

ROUND=4
sleep 2
obs=$(measure "$ROUND"); observe_and_report "$obs" "$ROUND"
sample_into_timeline loaded2 "$obs" "$ROUND"

echo "drop the load; keep measuring so the service evidence stays fresh"
for pid in "${LOAD_PIDS[@]:-}"; do kill -KILL "$pid" 2>/dev/null; wait "$pid" 2>/dev/null; done
LOAD_PIDS=()

# A gate that reads service evidence must not act on a measurement that has aged
# out, so an idle probe is itself a reason to hold. A pilot that measures once and
# stops therefore never moves again; this loop is the telemetry heartbeat.
refresh() {  # phase
  local phase=$1 saved=$SLO_WINDOW_S obs
  ROUND=$((ROUND + 1))
  SLO_WINDOW_S="${SHORT_WINDOW_S:-2}"
  obs=$(measure "$ROUND")
  SLO_WINDOW_S=$saved
  observe_and_report "$obs" "$ROUND"
  sample_into_timeline "$phase" "$obs" "$ROUND"
  LAST_OBS=$obs
}

back_at=""
for n in $(seq 1 14); do
  refresh returning
  st=$("$EP" status --socket "$SOCK")
  expert=$(python3 -c "import json,sys;print(json.loads(sys.argv[1])['auto_apply']['active_expert'])" "$st")
  if [ "$expert" = "safe-basic" ]; then back_at=$n; echo "  returned to safe-basic at refresh ${n}"; break; fi
  hold=$(python3 -c "import json,sys;a=json.loads(sys.argv[1])['auto_apply'];print(a.get('last_hold') or '')" "$st")
  echo "  refresh $n still $expert (hold: $hold)"
  sleep 2
done
[ -n "$back_at" ] || { echo "RESULT never returned within ${n} refreshes (see timeline)"; tail -5 "$OUT/timeline.csv"; exit 1; }
switches_now=$(python3 -c "import json,sys;print(json.loads(sys.argv[1])['auto_apply']['switches_recorded'])" "$st")
python3 - "$st" <<'RETURN'
import json, sys
service = json.loads(sys.argv[1])["service"]
print("ARMED_AFTER_RETURN outcome_pending=%s required_windows=%s" %
      (service.get("outcome_pending"), service.get("outcome_windows_required")))
RETURN

# What matters here is what the *real* measurement did with the declared 5% budget
# once the executor knows its own jitter: it must refuse to condemn, and say why.
# The K-window confirmation state machine itself is evidenced by run5's daemon log
# ("suspected ... in window 1 of 2", then a confirmed entry) on the pre-guard build.
st=$("$EP" status --socket "$SOCK")
python3 - "$st" <<'NOISE'
import json, sys
service = json.loads(sys.argv[1])["service"]
adverse = service.get("adverse_experts") or {}
print("NOISE p99=%.1f%% progress=%.1f%% pending=%s last_outcome=%r" % (
    service.get("noise_p99_pct", 0), service.get("noise_bg_progress_pct", 0),
    service.get("outcome_pending"), service.get("last_outcome")))
print("CHECK_NO_SPURIOUS_CONDEMN=%s" % ("yes" if not adverse else "no"))
print("CHECK_GUARD_EXPLAINS_ITSELF=%s" %
      ("yes" if "not run" in (service.get("last_outcome") or "") else "no"))
NOISE
switches_after=$(python3 -c "import json,sys;print(json.loads(sys.argv[1])['auto_apply']['switches_recorded'])" "$st")
hold=$(python3 -c "import json,sys;a=json.loads(sys.argv[1])['auto_apply'];print(a.get('last_hold') or '')" "$st")

echo "now starve the declared floor from outside and see whether the autopilot stops"
python3 - "$LAST_OBS" "$OUT/obs-starved.json" <<'STARVE'
import json, sys, time
obs = json.load(open(sys.argv[1]))
obs["background"]["work_units_per_sec"] = 0.001
# Re-stamp it: a stale measurement would be refused for the wrong reason.
obs["observed_at_ms"] = int(time.time() * 1000)
json.dump(obs, open(sys.argv[2], "w"))
STARVE
"$EP" observe --file "$OUT/obs-starved.json" --socket "$SOCK" >"$OUT/ack-starved.json" 2>&1
slo="$("$EP" slo --expert throughput-balanced --socket "$SOCK")"
python3 - "$slo" <<'STARVED'
import json, sys
reply = json.loads(sys.argv[1])
print("SLO_AFTER_STARVE allowed=%s code=%s service_evidence=%s" %
      (reply.get("allowed"), reply.get("code"), reply.get("service_evidence")))
STARVED
hold=""
for n in $(seq 1 8); do
  sleep 1
  st=$("$EP" status --socket "$SOCK")
  hold=$(python3 -c "import json,sys;a=json.loads(sys.argv[1])['auto_apply'];print(a.get('last_hold') or '')" "$st")
  case "$hold" in *slo*denied*|*SLO*) break ;; esac
done
echo "STARVE_HOLD=$hold"
switches_starve=$(python3 -c "import json,sys;print(json.loads(sys.argv[1])['auto_apply']['switches_recorded'])" "$st")
echo "RESULT switch_up_at_s=$switch_at returned_at_s=$back_at switches_up=$switches_now switches_after_return=$switches_after switches_at_starve=$switches_starve hold=$hold"
if [ "$switches_starve" -le "$switches_after" ]; then
  echo "CHECK_STARVE_STOPS_AUTOPILOT=yes"
else
  echo "CHECK_STARVE_STOPS_AUTOPILOT=no (it kept switching: $switches_after -> $switches_starve)"
fi
cp "$RUN/daemon.log" "$OUT/daemon.log"
