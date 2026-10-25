#!/usr/bin/env bash
# EulerPilot v2 -- continuous telemetry and the measured forward ceiling.
#
# Two claims from the measurement spec (docs/EXPERIMENT_PROTOCOL.md 5.1) have never
# been exercised together on a real machine: that a resident producer keeps the
# service gate from ever going stale, and that an *absolute* forward ceiling stops
# the automatic channel on measured grounds -- no injected numbers, no K-window
# confirmation, because a single-window fact needs no repeat.
#
# The baseline declares max_foreground_p99_ms at 1ms. Under the spinner load the
# foreground probe measures ~2ms, which is out of spec, so the autopilot must hold
# even though the router wants latency-balanced.
#
#   auto_apply_slo_ceiling_sp3.sh <ep-executord> <ep> <ep-sched-loader> [out-dir]
#
# Needs root, an idle sched_ext, a prior cmake build and service_load_probe compiled.
# Manual, not part of ctest.
set -uo pipefail

EXECUTORD="${1:?usage: auto_apply_slo_ceiling_sp3.sh <ep-executord> <ep> <ep-sched-loader> [out-dir]}"
EP="${2:?usage: auto_apply_slo_ceiling_sp3.sh <ep-executord> <ep> <ep-sched-loader> [out-dir]}"
LOADER="${3:?usage: auto_apply_slo_ceiling_sp3.sh <ep-executord> <ep> <ep-sched-loader> [out-dir]}"
OUT="${4:-/root/epv2-evidence/auto-slo-ceiling}"
HERE="$(cd "$(dirname "$0")" && pwd)"
CTL="$(dirname "$LOADER")/ep-schedctl"
PROBE="$HERE/service_load_probe"
OBSERVER="$HERE/service_observer_sp3.sh"
PIN_ROOT=/sys/fs/bpf/eulerpilot-v2
CEILING_MS="${CEILING_MS:-1}"
WINDOW_S="${WINDOW_S:-15}"
OBSERVE_S="${OBSERVE_S:-100}"
RUN=/tmp/ep-slo-ceil

[ "$(id -u)" = 0 ] || { echo "requires root"; exit 77; }
[ "$(cat /sys/kernel/sched_ext/state 2>/dev/null)" = "disabled" ] || { echo "sched_ext not idle"; exit 1; }
[ -z "$(pgrep -x ep-executord)" ] || { echo "executor already running"; exit 1; }
[ -x "$PROBE" ] || { echo "missing probe: $PROBE"; exit 1; }

mkdir -p "$OUT"
rm -rf "$RUN"; mkdir -p "$RUN/state" "$OUT/observer"
SOCK="$RUN/executor.sock"; BASELINE="$RUN/baseline.json"
DAEMON=""; OBS_PID=""; LOAD_PIDS=()

cleanup() {
  local rc=$?
  for pid in "${LOAD_PIDS[@]:-}" "$OBS_PID"; do
    [ -n "$pid" ] && kill -KILL "$pid" 2>/dev/null; wait "$pid" 2>/dev/null
  done
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

python3 - "$BASELINE" "$CEILING_MS" <<'PY'
import json, sys
json.dump({
  "apiVersion": "eulerpilot.io/v1alpha1", "kind": "SchedulingPolicy",
  "metadata": {"name": "ceiling-probe"},
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
               "max_bg_wait_ms": 5000, "max_foreground_p99_ms": int(sys.argv[2]),
               "fallback": "verified-safe-profile", "allow_affinity_change": False}
  }
}, open(sys.argv[1], "w"))
PY

"$EXECUTORD" --state-dir "$RUN/state" --socket "$SOCK" --skill sched \
  --loader "$LOADER" --sched-ctl "$CTL" \
  --auto-apply --auto-policy "$BASELINE" --auto-owner autopilot-v2 \
  --auto-min-interval-ms 3000 --auto-lease-ttl-ms 900000 \
  --auto-dwell-ms 1000 --auto-candidate-hold-ms 1000 \
  --auto-budget-window-ms 600000 --auto-budget-max-switches 10 \
  >"$RUN/daemon.log" 2>&1 &
DAEMON=$!
ready=""
for _ in $(seq 1 100); do
  if "$EP" status --socket "$SOCK" 2>/dev/null | grep -q '"service"'; then ready=1; break; fi
  sleep 0.1
done
[ -n "$ready" ] || { echo "FATAL executor never answered"; cat "$RUN/daemon.log"; exit 1; }

"$EP" apply --file "$BASELINE" --idempotency-key ceil-apply-1 --ttl-ms 900000 \
  --socket "$SOCK" >"$OUT/apply.json" 2>&1
"$EP" lease --owner autopilot-v2 --ttl-ms 900000 --socket "$SOCK" >"$OUT/handover.json" 2>&1

bash "$OBSERVER" "$EP" "$SOCK" "$PROBE" "$OUT/observer" "$WINDOW_S" 1 300 >/dev/null 2>&1 &
OBS_PID=$!
echo "resident observer started (window ${WINDOW_S}s)"
sleep $((WINDOW_S + 2))

for _ in $(seq 1 $(( $(nproc) * 2 ))); do
  (while :; do :; done) </dev/null >/dev/null 2>&1 & LOAD_PIDS+=("$!")
done
echo "load raised; waiting for a measured p99 above the ${CEILING_MS}ms ceiling"

breached=""
rows="$OUT/observer/observer.csv"
for n in $(seq 1 "$OBSERVE_S"); do
  sleep 1
  if [ -f "$rows" ] && awk -F, 'NR>1 && $5+0 > '"$CEILING_MS"' {found=1} END{exit !found}' "$rows"; then
    breached=$n; break
  fi
done
if [ -z "$breached" ]; then
  echo "RESULT never measured p99 above ${CEILING_MS}ms in ${OBSERVE_S}s"
  tail -5 "$rows" 2>/dev/null
  exit 1
fi
echo "  measured breach at +${breached}s"

st="$("$EP" status --socket "$SOCK")"
printf '%s' "$st" > "$OUT/status-at-breach.json"
python3 - "$st" <<'REPORT'
import json, sys
reply = json.loads(sys.argv[1])
service = reply["service"]
auto = reply["auto_apply"]
print("AT_BREACH expert=%s switches=%s hold=%r" %
      (auto.get("active_expert"), auto.get("switches_recorded"), auto.get("last_hold")))
print("AT_BREACH measured p99=%s ceiling=%s adverse=%s" % (
    service.get("foreground_p99_ms"),
    (service.get("declared_limits") or {}).get("max_foreground_p99_ms"),
    json.dumps(service.get("adverse_experts"), sort_keys=True)))
REPORT
switches_at_breach=$(printf '%s' "$st" |
  python3 -c "import json,sys;print(json.load(sys.stdin)['auto_apply']['switches_recorded'])")
hold_at_breach=$(printf '%s' "$st" |
  python3 -c "import json,sys;a=json.load(sys.stdin)['auto_apply'];print(a.get('last_hold') or '')")
slo="$("$EP" slo --expert latency-balanced --socket "$SOCK")"
printf '%s' "$slo" > "$OUT/slo-at-breach.json"
python3 - "$slo" <<'SLO'
import json, sys
reply = json.loads(sys.argv[1])
print("SLO_LATENCY allowed=%s code=%s" % (reply.get("allowed"), reply.get("code")))
print("CHECK_CEILING_DENIES_ON_MEASURED_BREACH=%s" %
      ("yes" if reply.get("code") == "FOREGROUND_P99_EXCEEDED" and reply.get("allowed") is False
       else "no"))
SLO

sleep $((WINDOW_S + 5))
st="$("$EP" status --socket "$SOCK")"
switches_later=$(printf '%s' "$st" |
  python3 -c "import json,sys;print(json.load(sys.stdin)['auto_apply']['switches_recorded'])")
no_stale=$(printf '%s' "$st" | python3 -c "import json,sys;a=json.load(sys.stdin)['auto_apply'];print('no' if 'STALE' in (a.get('last_hold') or '') else 'yes')")
if [ "$switches_later" -eq "$switches_at_breach" ]; then
  echo "CHECK_HELD_ON_BREACH=yes (switches stayed $switches_later)"
else
  echo "CHECK_HELD_ON_BREACH=no ($switches_at_breach -> $switches_later)"
fi
echo "CHECK_TELEMETRY_NOT_STALE=${no_stale:-unknown}"
for pid in "${LOAD_PIDS[@]:-}"; do kill -KILL "$pid" 2>/dev/null; wait "$pid" 2>/dev/null; done
LOAD_PIDS=()
sleep $((WINDOW_S + 5))
st="$("$EP" status --socket "$SOCK")"
printf '%s' "$st" > "$OUT/status-after-drop.json"
python3 - "$st" <<'AFTER'
import json, sys
reply = json.loads(sys.argv[1])
print("AFTER_DROP expert=%s switches=%s hold=%r p99=%s age_ms=%s accepted=%s" % (
    reply["auto_apply"].get("active_expert"), reply["auto_apply"].get("switches_recorded"),
    reply["auto_apply"].get("last_hold"), reply["service"].get("foreground_p99_ms"),
    reply["service"].get("age_ms"), reply["service"].get("observations_accepted")))
AFTER

# The ceiling check reads the newest accepted measurement, so recovery can only be
# shown by waiting for a *compliant* window to land -- sampling immediately after
# the load drops just re-reads the breaching window (that is what run2 did).
echo "waiting for a compliant window (p99 under ${CEILING_MS}ms) to land"
recovered=""
for n in $(seq 1 12); do
  sleep 5
  st="$("$EP" status --socket "$SOCK")"
  pair=$(printf '%s' "$st" | python3 -c "import json,sys;s=json.load(sys.stdin)['service'];print('%s %s' % (s.get('foreground_p99_ms') if s.get('foreground_p99_ms') is not None else -1, s.get('age_ms') if s.get('age_ms') is not None else -1))")
  p99=${pair% *}; age=${pair#* }
  if python3 -c "import sys;sys.exit(0 if (0 <= float('$age') <= 4000 and 0 <= float('$p99') < $CEILING_MS) else 1)"; then
    recovered=$n; echo "  compliant window after ${n} polls: p99=${p99}ms age=${age}ms"; break;
  fi
done
if [ -z "$recovered" ]; then
  echo "CHECK_GATE_REOPENS_AFTER_RECOVERY=no (no compliant window within 60s; p99=$p99 age=$age)"
else
  slo="$("$EP" slo --expert latency-balanced --socket "$SOCK")"
  printf '%s' "$slo" > "$OUT/slo-after-recovery.json"
  code=$(printf '%s' "$slo" | python3 -c "import json,sys;print(json.load(sys.stdin).get('code') or '')")
  hold=$(printf '%s' "$st" | python3 -c "import json,sys;print(json.load(sys.stdin)['auto_apply'].get('last_hold') or '')")
  if [ "$code" != "FOREGROUND_P99_EXCEEDED" ] && [ "$hold" != "slo denied: FOREGROUND_P99_EXCEEDED" ]; then
    echo "CHECK_GATE_REOPENS_AFTER_RECOVERY=yes (code=${code:-none} hold=${hold:-none})"
  else
    echo "CHECK_GATE_REOPENS_AFTER_RECOVERY=no (code=$code hold=$hold)"
  fi
fi

# Stop the producer before comparing counters: taking the consumer's snapshot
# while the producer is still ingesting compares two different instants (that is
# exactly what made run4 report 6 vs 5).
kill -TERM "$OBS_PID" 2>/dev/null; wait "$OBS_PID" 2>/dev/null; OBS_PID=""
sleep 2
st="$("$EP" status --socket "$SOCK")"
printf '%s' "$st" > "$OUT/status-final.json"
if [ -f "$OUT/observer/observer.summary.json" ]; then
  cp "$OUT/observer/observer.summary.json" "$OUT/observer.summary.json"
  ingested=$(python3 -c "import json;print(json.load(open('$OUT/observer.summary.json'))['ingested'])")
  coverage=$(python3 -c "import json;print(json.load(open('$OUT/observer.summary.json'))['coverage_pct'])")
  accepted=$(printf '%s' "$st" | python3 -c "import json,sys;print(json.load(sys.stdin)['service'].get('observations_accepted'))")
  rejected=$(printf '%s' "$st" | python3 -c "import json,sys;print(json.load(sys.stdin)['service'].get('observations_rejected'))")
  echo "TELEMETRY rejections_executor=$rejected last_rejection=$(printf '%s' "$st" | python3 -c "import json,sys;print(json.load(sys.stdin)['service'].get('last_rejection') or '')")"
  echo "TELEMETRY ingested=$ingested accepted_by_executor=$accepted rejected_by_executor=$rejected coverage=${coverage}%"
  if [ "$ingested" = "$accepted" ]; then
    echo "CHECK_OBSERVER_AND_EXECUTOR_AGREE=yes"
  else
    echo "CHECK_OBSERVER_AND_EXECUTOR_AGREE=no (producer $ingested vs consumer $accepted)"
  fi
  if python3 -c "import sys;sys.exit(0 if float('$coverage') >= 80.0 else 1)"; then
    echo "CHECK_OBSERVATION_COVERAGE=yes (${coverage}% >= 80%)"
  else
    echo "CHECK_OBSERVATION_COVERAGE=no (${coverage}%)"
  fi
else
  echo "CHECK_OBSERVER_AND_EXECUTOR_AGREE=no (no summary written: observer died)"
fi
cp "$RUN/daemon.log" "$OUT/daemon.log"
cp "$OUT/observer/observer.csv" "$OUT/observer.csv"
echo "RESULT ceiling_ms=$CEILING_MS window_s=$WINDOW_S breach_at_s=$breached switches_at_breach=$switches_at_breach hold=$hold_at_breach"
