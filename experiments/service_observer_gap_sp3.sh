#!/usr/bin/env bash
# EulerPilot v2 -- what an observation gap looks like from the consumer side.
#
# The service gate refuses to act on a measurement older than two of its windows,
# so a producer that dies silently makes the automatic channel look careful while
# it is actually frozen. Two things have to be true before that is operable:
#
#   1. the control plane must still report the gap after it ends (age_ms exists
#      only while someone asks; observation_max_age_ms and stale_checks are
#      remembered), and
#   2. a watchdog must be able to say "clean" while nothing is wrong, or its
#      alarms are worthless.
#
# This run drives a real producer death and recovery on SP3. Suppression is pinned
# at 1h and the ceilings are set out of reach, so the only gate that may bind is
# freshness -- and because the evidence layer is consulted before the suppression
# layer, whatever the pilot reports as its hold reason is the binding one.
#
#   service_observer_gap_sp3.sh <ep-executord> <ep> <ep-sched-loader> [out-dir]
#
# Needs root, an idle sched_ext, a prior cmake build, and service_load_probe /
# service_observer_sp3.sh / service_watchdog_sp3.sh next to this script.
# Manual, not part of ctest.
set -uo pipefail

EXECUTORD="${1:?usage: service_observer_gap_sp3.sh <ep-executord> <ep> <ep-sched-loader> [out-dir]}"
EP="${2:?missing ep}"
LOADER="${3:?missing ep-sched-loader}"
OUT="${4:-/root/epv2-evidence/observer-gap}"
HERE="$(cd "$(dirname "$0")" && pwd)"
CTL="$(dirname "$LOADER")/ep-schedctl"
PROBE="$HERE/service_load_probe"
OBSERVER="$HERE/service_observer_sp3.sh"
WATCHDOG="$HERE/service_watchdog_sp3.sh"
PIN_ROOT=/sys/fs/bpf/eulerpilot-v2
WINDOW_S="${WINDOW_S:-5}"
HEALTHY_S="${HEALTHY_S:-24}"
GAP_S="${GAP_S:-25}"
RUN=/tmp/ep-observer-gap

[ "$(id -u)" = 0 ] || { echo "requires root"; exit 77; }
[ "$(cat /sys/kernel/sched_ext/state 2>/dev/null)" = "disabled" ] || { echo "sched_ext not idle"; exit 1; }
[ -z "$(pgrep -x ep-executord)" ] || { echo "executor already running"; exit 1; }
for need in "$PROBE" "$OBSERVER" "$WATCHDOG"; do
  [ -x "$need" ] || { echo "missing $need"; exit 1; }
done

mkdir -p "$OUT"
rm -rf "$RUN"; mkdir -p "$RUN/state" "$OUT/observer" "$OUT/observer2" \
  "$OUT/watchdog-clean" "$OUT/watchdog-gap"
SOCK="$RUN/executor.sock"; BASELINE="$RUN/baseline.json"
DAEMON=""; OBS_PID=""; WATCH_PID=""

cleanup() {
  local rc=$?
  for pid in "$OBS_PID" "$WATCH_PID"; do
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

python3 - "$BASELINE" <<'BASELINE'
import json, sys
json.dump({
  "apiVersion": "eulerpilot.io/v1alpha1", "kind": "SchedulingPolicy",
  "metadata": {"name": "observer-gap"},
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
               "max_bg_wait_ms": 10000, "max_foreground_p99_ms": 60000,
               "fallback": "verified-safe-profile", "allow_affinity_change": False}
  }
}, open(sys.argv[1], "w"))
BASELINE

"$EXECUTORD" --state-dir "$RUN/state" --socket "$SOCK" --skill sched \
  --loader "$LOADER" --sched-ctl "$CTL" \
  --auto-apply --auto-policy "$BASELINE" --auto-owner autopilot-v2 \
  --auto-min-interval-ms 3000 --auto-lease-ttl-ms 900000 \
  --auto-dwell-ms 3600000 --auto-candidate-hold-ms 1000 \
  --auto-budget-window-ms 600000 --auto-budget-max-switches 10 \
  >"$RUN/daemon.log" 2>&1 &
DAEMON=$!
ready=""
for _ in $(seq 1 100); do
  if "$EP" status --socket "$SOCK" 2>/dev/null | grep -q '"service"'; then ready=1; break; fi
  sleep 0.1
done
[ -n "$ready" ] || { echo "FATAL executor never answered"; cat "$RUN/daemon.log"; exit 1; }

"$EP" apply --file "$BASELINE" --idempotency-key gap-apply-0001 --ttl-ms 900000 \
  --socket "$SOCK" >"$OUT/apply.json" 2>&1
apply_status=$(python3 -c "import json;print(json.load(open('$OUT/apply.json')).get('status'))")
[ "$apply_status" = "VERIFIED" ] || { echo "FATAL apply returned $apply_status"; tail -5 "$OUT/apply.json"; exit 1; }
"$EP" lease --owner autopilot-v2 --ttl-ms 900000 --socket "$SOCK" >"$OUT/handover.json" 2>&1

echo "=== phase 1: healthy producer; the watchdog must find nothing"
# interval 1s, not 5s: a cycle costs window+interval, and the freshness rule is
# age <= 2x window. Producing once per 2x window would flap on a healthy box.
bash "$OBSERVER" "$EP" "$SOCK" "$PROBE" "$OUT/observer" "$WINDOW_S" 1 1200 >/dev/null 2>&1 &
OBS_PID=$!
first=""
for _ in $(seq 1 30); do
  sleep 1
  if "$EP" status --socket "$SOCK" 2>/dev/null |
     python3 -c "import json,sys;sys.exit(0 if json.load(sys.stdin)['service'].get('observations_accepted',0)>0 else 1)"; then
    first=1; break
  fi
done
[ -n "$first" ] || { echo "RESULT producer never landed an observation"; exit 1; }

bash "$WATCHDOG" "$EP" "$SOCK" "$OUT/watchdog-clean" 2 "$HEALTHY_S" \
  "$OUT/observer/observer.heartbeat" "$OUT/observer/observer.csv" \
  >"$OUT/watchdog-clean.out" 2>&1
clean_rc=$?
echo "  healthy watchdog rc=$clean_rc"
python3 - "$OUT/watchdog-clean/watchdog.summary.json" <<'CLEANREPORT'
import json, sys
try:
    print("WATCHDOG_CLEAN %s" % json.dumps(json.load(open(sys.argv[1])), sort_keys=True))
except Exception as exc:
    print("WATCHDOG_CLEAN summary unreadable: %r" % exc)
CLEANREPORT
[ "$clean_rc" = "0" ] && echo "CHECK_WATCHDOG_CLEAN_ON_HEALTHY=yes" \
  || { echo "CHECK_WATCHDOG_CLEAN_ON_HEALTHY=no"; cat "$OUT/watchdog-clean/watchdog.alerts"; }

echo "=== phase 2: producer killed (pid $OBS_PID)"
kill -KILL "$OBS_PID" 2>/dev/null; wait "$OBS_PID" 2>/dev/null
OBS_PID=""
# Thresholds tightened for the gap run because the gap is 25s, not 60s. No
# observer.csv is handed over: this run performs its own manual ingests, so the
# producer/control-plane row counts would disagree by construction.
ALERT_MAX_AGE_MS=15000 HEARTBEAT_LIMIT_MS=15000 \
  bash "$WATCHDOG" "$EP" "$SOCK" "$OUT/watchdog-gap" 2 $((GAP_S + 90)) \
  "$OUT/observer/observer.heartbeat" >"$OUT/watchdog-gap.out" 2>&1 &
WATCH_PID=$!
sleep "$GAP_S"

"$EP" status --socket "$SOCK" >"$OUT/status-in-gap.json" 2>&1
"$EP" slo --expert safe-basic --socket "$SOCK" >"$OUT/slo-in-gap.json" 2>&1
python3 - "$OUT/status-in-gap.json" "$OUT/slo-in-gap.json" <<'GAPREPORT'
import json, sys
s = json.load(open(sys.argv[1]))["service"]
a = json.load(open(sys.argv[1]))["auto_apply"]
hold = a.get("last_hold") or ""
slo = json.load(open(sys.argv[2]))
print("IN_GAP age_ms=%s window_ms=%s max_age_ms=%s stale_checks=%s" %
      (s.get("age_ms"), s.get("window_ms"), s.get("observation_max_age_ms"),
       s.get("stale_checks")))
print("IN_GAP hold=%r switches=%s slo_allowed=%s slo_code=%s" %
      (hold, a.get("switches_recorded"), slo.get("allowed"), slo.get("code")))
print("CHECK_GATE_DENIES_ON_STALE=%s" %
      ("yes" if slo.get("code") == "SERVICE_OBSERVATION_STALE" and slo.get("allowed") is False
       else "no"))
print("CHECK_EVIDENCE_OUTRANKS_SUPPRESSION=%s" %
      ("yes" if "SERVICE_OBSERVATION_STALE" in hold else "no"))
GAPREPORT

echo "=== phase 3: re-ingesting the dark period"
template=$(ls -1t "$OUT/observer"/obs-*.json 2>/dev/null | head -1)
if [ -z "$template" ]; then
  echo "RESULT no observation file to re-stamp"
else
  # A: late but still able to describe the present (within 10x its own window).
  # B: ancient. It must be refused rather than silently become "the present".
  python3 - "$template" "$RUN/late.json" "$RUN/ancient.json" <<'STAMP'
import json, sys, time
src, late_path, ancient_path = sys.argv[1:4]
now = int(time.time() * 1000)
late = json.load(open(src)); late["workload"] = "gap-late-replay"
late["observed_at_ms"] = now - 20000
json.dump(late, open(late_path, "w"))
ancient = json.load(open(src)); ancient["workload"] = "gap-ancient-replay"
ancient["observed_at_ms"] = now - 120000
json.dump(ancient, open(ancient_path, "w"))
print("STAMP late.observed_at_ms=%d ancient.observed_at_ms=%d window_ms=%d" %
      (late["observed_at_ms"], ancient["observed_at_ms"], late["window_ms"]))
STAMP

  "$EP" observe --file "$RUN/late.json" --socket "$SOCK" >"$OUT/observe-late.json" 2>&1
  late_rc=$?
  "$EP" slo --expert safe-basic --socket "$SOCK" >"$OUT/slo-after-late.json" 2>&1
  "$EP" status --socket "$SOCK" >"$OUT/status-after-late.json" 2>&1
  "$EP" observe --file "$RUN/ancient.json" --socket "$SOCK" >"$OUT/observe-ancient.json" 2>&1
  ancient_rc=$?
  "$EP" status --socket "$SOCK" >"$OUT/status-after-ancient.json" 2>&1
  echo "  late ingest rc=$late_rc, ancient ingest rc=$ancient_rc"
  python3 - "$OUT/status-after-late.json" "$OUT/status-after-ancient.json" \
    "$OUT/slo-after-late.json" "$late_rc" "$ancient_rc" <<'REPLAY'
import json, sys
late = json.load(open(sys.argv[1]))["service"]
after = json.load(open(sys.argv[2]))["service"]
slo = json.load(open(sys.argv[3]))
late_rc, ancient_rc = int(sys.argv[4]), int(sys.argv[5])
print("REPLAY late_accepted=%s rejected_before=%s rejected_after=%s" %
      (late.get("observations_accepted"), late.get("observations_rejected"),
       after.get("observations_rejected")))
print("REPLAY last_rejection=%r observed_at_ms=%s (late was %s)" %
      (after.get("last_rejection"), after.get("observed_at_ms"), late.get("observed_at_ms")))
print("REPLAY slo_code_after_late=%s" % slo.get("code"))
print("CHECK_LATE_INGEST_TOLERATED=%s" % ("yes" if late_rc == 0 else "no"))
print("CHECK_LATE_INGEST_STILL_DENIES_ACTION=%s" %
      ("yes" if slo.get("code") == "SERVICE_OBSERVATION_STALE" and slo.get("allowed") is False
       else "no"))
print("CHECK_ANCIENT_INGEST_REFUSED=%s" %
      ("yes" if ancient_rc != 0 and "too old" in (after.get("last_rejection") or "") else "no"))
print("CHECK_ANCIENT_INGEST_DID_NOT_DISPLACE=%s" %
      ("yes" if after.get("observed_at_ms") == late.get("observed_at_ms") and
       after.get("observations_rejected", 0) == late.get("observations_rejected", 0) + 1
       else "no"))
REPLAY
fi

echo "=== phase 4: producer restarted; the gap must still be reported"
bash "$OBSERVER" "$EP" "$SOCK" "$PROBE" "$OUT/observer2" "$WINDOW_S" 1 300 >/dev/null 2>&1 &
OBS_PID=$!
recovered=""
for n in $(seq 1 20); do
  sleep 2
  st="$("$EP" status --socket "$SOCK")"
  if printf '%s' "$st" | python3 -c "import json,sys;s=json.load(sys.stdin)['service'];sys.exit(0 if s.get('observed') and s['age_ms'] <= 2*s['window_ms'] else 1)"; then
    recovered=$n
    printf '%s' "$st" > "$OUT/status-after-recovery.json"
    break
  fi
done
if [ -z "$recovered" ]; then
  echo "RESULT never recovered a fresh window"
  kill -KILL "$WATCH_PID" 2>/dev/null; wait "$WATCH_PID" 2>/dev/null
  exit 1
fi
echo "  fresh window landed after ${recovered} polls"
python3 - "$OUT/status-after-recovery.json" <<'RECOVERED'
import json, sys
doc = json.load(open(sys.argv[1]))
s, a = doc["service"], doc["auto_apply"]
print("AFTER_RECOVERY age_ms=%s max_age_ms=%s stale_checks=%s accepted=%s rejected=%s" %
      (s.get("age_ms"), s.get("observation_max_age_ms"), s.get("stale_checks"),
       s.get("observations_accepted"), s.get("observations_rejected")))
print("AFTER_RECOVERY hold=%r" % (a.get("last_hold") or ""))
print("CHECK_GAP_REMEMBERED_AFTER_RECOVERY=%s" %
      ("yes" if s.get("age_ms", 10 ** 9) <= 2 * s.get("window_ms", 0) and
       s.get("observation_max_age_ms", 0) > 2 * s.get("window_ms", 0) and
       s.get("stale_checks", 0) >= 1 else "no"))
print("CHECK_STALE_HOLD_CLEARED=%s" %
      ("yes" if "SERVICE_OBSERVATION_STALE" not in (a.get("last_hold") or "") else "no"))
RECOVERED

kill -TERM "$WATCH_PID" 2>/dev/null; wait "$WATCH_PID" 2>/dev/null
watch_rc=$?
python3 - "$OUT/watchdog-gap/watchdog.summary.json" "$OUT/watchdog-gap/watchdog.alerts" <<'WDR'
import json, sys
try:
    print("WATCHDOG_GAP %s" % json.dumps(json.load(open(sys.argv[1])), sort_keys=True))
except Exception as exc:
    print("WATCHDOG_GAP summary unreadable: %r" % exc)
text = open(sys.argv[2]).read()
for kind in ("STALE_EPISODE", "STALE_NOW", "STALE_MEMORY", "PRODUCER_SILENT", "REJECTIONS"):
    print("CHECK_WATCHDOG_SAW_%s=%s" % (kind, "yes" if kind in text else "no"))
WDR
echo "  gap watchdog rc=$watch_rc (must be nonzero)"
[ "$watch_rc" != "0" ] && echo "CHECK_WATCHDOG_EXITS_ON_BREACH=yes" \
  || echo "CHECK_WATCHDOG_EXITS_ON_BREACH=no"
