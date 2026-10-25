#!/usr/bin/env bash
# EulerPilot v2 -- does the service gate earn its keep when it actually fires?
#
# The ablation round compared D against a D whose gate could never bind and got a null,
# but the honest reading was "the gate never fired", not "the gate is useless": in all
# 30 adaptive blocks the hold reason was "already in place", with zero denials. This
# driver builds the missing condition and then measures:
#
#   1. a quiet baseline measurement (autopilot does NOT hold the lease yet, so nothing
#      can switch before the condition exists);
#   2. inject CPU contention (2 x nproc spinners) so the served workload's p99 and the
#      background progress move -- the observer ingests two windows of that;
#   3. hand the lease over *while* the condition holds, and measure what each arm does:
#      D  (gate binding) must refuse the switch and say why,
#      Dng (thresholds unable to bind) is free to switch;
#   4. stop the injection and let both recover.
#
# The run fails loudly if either premise does not hold (D never denied, or Dng never
# switched): a scenario that did not reproduce its own condition cannot support a
# conclusion either way.
#
#   gate_injection_sp3.sh <ep-executord> <ep> <ep-sched-loader> [out-dir] [repeats] [rate]
#
# Needs root, idle sched_ext, redis-server, built probes. Manual, not part of ctest.
set -uo pipefail

EXECUTORD="${1:?usage: gate_injection_sp3.sh <ep-executord> <ep> <ep-sched-loader> [out-dir] [repeats] [rate]}"
EP="${2:?missing ep}"
LOADER="${3:?missing ep-sched-loader}"
OUT="${4:-/root/epv2-evidence/gate-injection}"
REPEATS="${5:-3}"
RATE="${6:-60000}"
HERE="$(cd "$(dirname "$0")" && pwd)"
CTL="$(dirname "$LOADER")/ep-schedctl"
PROBE="$HERE/redis_openloop_probe"
SVC="$HERE/service_load_probe"
OBSERVER="$HERE/service_observer_sp3.sh"
PIN_ROOT=/sys/fs/bpf/eulerpilot-v2
CONNS=8
KEYPOOL=100000
SPINNERS="${SPINNERS:-$(( $(nproc) * 2 ))}"
# A noisy neighbour that merely competes at equal weight moves the probe's p99 by a few
# hundred microseconds, i.e. into the noise band around any tight budget, and the gate
# then opens on whichever window happens to land under it. Raising the neighbour's
# weight (root may set a negative nice) makes the breach unambiguous for the whole
# injection, which is the condition this driver needs.
SPINNER_NICE="${SPINNER_NICE:-0}"
# CPU spinners move a 1ms-wake probe's p99 by only a few hundred microseconds on this
# scheduler: the wake is still served promptly. A wake-up storm (many 1ms sleepers)
# stresses exactly what that probe measures while leaving throughput available, so it
# is the lever that can put the probe's p99 decisively above a budget.
WAKERS="${WAKERS:-0}"
PORT="${PORT:-7803}"
BG_WINDOW_S=5
OBS_WINDOW_S=5
# Overridable so the scenario can be smoke-tested without spending a full block.
BASELINE_S="${BASELINE_S:-30}"
SETTLE_S="${SETTLE_S:-10}"
MEASURE_S="${MEASURE_S:-60}"
RECOVER_S="${RECOVER_S:-20}"
RUN=/tmp/ep-gate-injection
CG_SVC=/sys/fs/cgroup/gateinj-svc
CG_BG=/sys/fs/cgroup/gateinj-bg
SOCK="$RUN/executor.sock"

[ "$(id -u)" = 0 ] || { echo "requires root"; exit 77; }
[ "$(cat /sys/kernel/sched_ext/state 2>/dev/null)" = "disabled" ] || { echo "sched_ext not idle"; exit 1; }
[ -z "$(pgrep -x ep-executord)" ] || { echo "executor already running"; exit 1; }
for need in "$PROBE" "$SVC" "$OBSERVER"; do
  [ -x "$need" ] || { echo "missing $need"; exit 1; }
done

mkdir -p "$OUT" "$CG_SVC" "$CG_BG"
DAEMON=""; REDIS_PID=""; BG_PID=""; OBS_PID=""; POLL_PID=""
LOAD_PIDS=()

cleanup() {
  local rc=$?
  for pid in "$POLL_PID" "$OBS_PID" "$BG_PID" "${LOAD_PIDS[@]:-}"; do
    [ -n "${pid:-}" ] && kill -KILL "$pid" 2>/dev/null; wait "$pid" 2>/dev/null
  done
  if [ -n "$DAEMON" ] && kill -0 "$DAEMON" 2>/dev/null; then
    kill -TERM "$DAEMON" 2>/dev/null; wait "$DAEMON" 2>/dev/null
  fi
  if [ -n "$REDIS_PID" ] && kill -0 "$REDIS_PID" 2>/dev/null; then
    redis-cli -p "$PORT" shutdown nosave >/dev/null 2>&1
    kill -TERM "$REDIS_PID" 2>/dev/null; wait "$REDIS_PID" 2>/dev/null
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
  # Fallback sweep: this driver runs exclusively, so any pin left after the daemon is
  # gone and sched_ext is disabled is ours. Without it the last arm leaked one directory
  # (observed once) and every later "clean machine" check would be wrong.
  if [ "$(cat /sys/kernel/sched_ext/state 2>/dev/null)" = "disabled" ] &&
     [ -z "$(pgrep -x ep-executord)" ]; then
    for dir in "$PIN_ROOT"/inst-*; do [ -d "$dir" ] && rm -rf "$dir"; done
  fi
  rmdir "$PIN_ROOT" 2>/dev/null
  # wait for the scratch cgroups to actually disappear before the next repeat reuses them
  for _ in $(seq 1 40); do
    { [ ! -d "$CG_SVC" ] && [ ! -d "$CG_BG" ]; } && break
    rmdir "$CG_SVC" "$CG_BG" 2>/dev/null
    sleep 0.25
  done
  echo "cleanup rc=$rc state=$(cat /sys/kernel/sched_ext/state 2>/dev/null) pins=$(ls -1 "$PIN_ROOT" 2>/dev/null | wc -l)"
}
trap cleanup EXIT

policy_json() {  # file arm -- D binds, Dng cannot bind
  python3 - "$1" "$2" "$CG_SVC" "$CG_BG" <<'POLICYGEN'
import json, os, sys
path, arm, svc_cg, bg_cg = sys.argv[1:5]
# The budget is set *below* what this service delivers even when healthy (measured
# quiet p99 ~1.7ms, and 0.23ms once CPUs are kept awake by load; loaded contention
# pushes it to 2.0-3.3ms). A budget of 1ms is therefore breached for the whole run, so
# the gate is continuously engaged and the premise "D never acts" is checkable. The
# earlier 2ms variant is what a real SLO would look like here, but this probe cannot
# resolve it against its own noise band: the gate opened on whichever window landed
# under budget (observed 11-13 denials then a switch). That is reported, not hidden.
gate = {"min_bg_work_units_per_sec": 140.0, "max_bg_wait_ms": 50,
        "max_foreground_p99_ms": int(float(os.environ.get("GATE_CEILING_MS", "1")))}
if arm == "Dng":
    # Same schema-required fields, values that cannot bind (floor must be > 0).
    gate = {"min_bg_work_units_per_sec": 0.001, "max_bg_wait_ms": 10000,
            "max_foreground_p99_ms": 60000}
json.dump({
  "apiVersion": "eulerpilot.io/v1alpha1", "kind": "SchedulingPolicy",
  "metadata": {"name": "gateinj-%s" % arm.lower()},
  "spec": {
    "scope": "scheduler-instance", "mode": "class", "expert": "safe-basic",
    "source": "human",
    "targets": [
      {"kind": "cgroup", "path": svc_cg, "role": "FOREGROUND", "role_source": "explicit"},
      {"kind": "cgroup", "path": bg_cg, "role": "BACKGROUND", "role_source": "explicit"}
    ],
    "parameters": {"lat_slice_ns": 1000000, "comp_slice_ns": 4000000,
                   "cache_slice_ns": 4000000, "bg_slice_ns": 2000000,
                   "preempt_threshold_ns": 500000, "bg_vtime_pct": 200,
                   "llc_affinity": False},
    "safety": {"min_dwell_ms": 5000, "max_observation_age_ms": 1000,
               "heartbeat_period_ms": 250, "heartbeat_timeout_ms": 2000,
               "policy_ttl_ms": 900000, "quiet_max_regression_pct": 5,
               "evaluation_min_bg_progress_ratio": 0.5, "bg_work_unit": "probe5s",
               "bg_progress_window_ms": 5000,
               "min_bg_work_units_per_sec": gate["min_bg_work_units_per_sec"],
               "max_bg_wait_ms": gate["max_bg_wait_ms"],
               "max_foreground_p99_ms": gate["max_foreground_p99_ms"],
               "fallback": "verified-safe-profile", "allow_affinity_change": False}
  }
}, open(path, "w"))
POLICYGEN
}

run_arm() {  # repeat arm
  local rep=$1 arm=$2 d="$OUT/r$1-$2"
  mkdir -p "$d" "$RUN/state"
  rm -rf "$RUN/state"; mkdir -p "$RUN/state"
  policy_json "$d/policy.json" "$arm"

  # A fresh redis under the declared service cgroup, populated before measuring so the
  # GET load is real data and identical across arms.
  redis-server --port "$PORT" --daemonize no --save '' --appendonly no --io-threads 1 \
    --maxmemory 512mb --logfile "$d/redis.log" >"$d/redis.stdout" 2>&1 9>&- &
  REDIS_PID=$!
  echo "$REDIS_PID" > "$CG_SVC/cgroup.procs"
  for _ in $(seq 1 100); do redis-cli -p "$PORT" ping >/dev/null 2>&1 && break; sleep 0.2; done
  python3 - "$KEYPOOL" > "$RUN/populate.redis" <<'GEN'
import sys
n = int(sys.argv[1])
out = sys.stdout.buffer
for i in range(n):
    key = ("k%d" % (i % n)).encode()
    out.write(b"*3\r\n$3\r\nSET\r\n$%d\r\n%s\r\n$64\r\n%s\r\n" % (len(key), key, b"a" * 64))
GEN
  redis-cli -p "$PORT" --pipe < "$RUN/populate.redis" > "$d/populate.out" 2>&1

  "$EXECUTORD" --state-dir "$RUN/state" --socket "$SOCK" --skill sched \
    --loader "$LOADER" --sched-ctl "$CTL" \
    --auto-apply --auto-policy "$d/policy.json" --auto-owner autopilot-v2 \
    --auto-min-interval-ms 3000 --auto-lease-ttl-ms 900000 \
    --auto-dwell-ms 5000 --auto-candidate-hold-ms 5000 \
    --auto-budget-window-ms 900000 --auto-budget-max-switches 8 \
    >"$d/daemon.log" 2>&1 &
  DAEMON=$!
  for _ in $(seq 1 100); do "$EP" status --socket "$SOCK" >/dev/null 2>&1 && break; sleep 0.1; done
  "$EP" apply --file "$d/policy.json" --idempotency-key "gateinj-$rep-$arm" --ttl-ms 900000 \
    --socket "$SOCK" >"$d/apply.json" 2>&1
  local status
  status=$(python3 -c "import json;print(json.load(open('$d/apply.json')).get('status'))")
  if [ "$status" != "VERIFIED" ]; then
    echo "$arm: apply returned $status" >> "$d/block.invalid"; stop_all; return 1
  fi

  # The observer runs outside both declared cgroups: its foreground probe is a *measuring*
  # task, not the served workload, and its background probe must not inherit the service
  # tier. Only redis (CG_SVC) and the harness bg producer (CG_BG) are declared targets.
  (
    bash "$OBSERVER" "$EP" "$SOCK" "$SVC" "$d/observer" "$OBS_WINDOW_S" 1 600 >/dev/null 2>&1
  ) 9>&- &
  OBS_PID=$!
  (
    echo "$BASHPID" > "$CG_BG/cgroup.procs"
    while :; do "$SVC" bg safe-basic "$BG_WINDOW_S"; done
  ) >>"$d/bg.ndjson" 2>>"$d/bg.err" 9>&- &
  BG_PID=$!

  # Phase 1: quiet baseline. The autopilot is configured but does not hold the lease,
  # so whatever it would like to do cannot happen yet.
  echo "  [$rep/$arm] baseline ${BASELINE_S}s (no lease to the autopilot)"
  "$PROBE" "$CONNS" "$RATE" "$BASELINE_S" GET --port "$PORT" --keypool "$KEYPOOL" \
    --slo-ms 5 --timeout-ms 50 --raw "$d/raw-baseline.bin" > "$d/baseline.json" 2>>"$d/probe.err"
  "$EP" status --socket "$SOCK" > "$d/status-baseline.json" 2>&1

  # Phase 2: contention, and time for the observer to ingest windows that describe it.
  echo "  [$rep/$arm] injecting ${SPINNERS} spinners + ${WAKERS} 1ms wakers (nice ${SPINNER_NICE}), settling ${SETTLE_S}s"
  for _ in $(seq 1 "$SPINNERS"); do
    if [ "$SPINNER_NICE" != "0" ]; then
      nice -n "$SPINNER_NICE" bash -c 'while :; do :; done' </dev/null >/dev/null 2>&1 9>&- &
    else
      (while :; do :; done) </dev/null >/dev/null 2>&1 9>&- &
    fi
    LOAD_PIDS+=("$!")
  done
  for _ in $(seq 1 "$WAKERS"); do
    python3 -c '
import time
end = time.time() + 600
while time.time() < end:
    time.sleep(0.001)
' >/dev/null 2>&1 9>&- &
    LOAD_PIDS+=("$!")
  done
  sleep "$SETTLE_S"

  # Phase 3: hand the lease over under the condition, then measure what happens.
  "$EP" lease --owner autopilot-v2 --ttl-ms 900000 --socket "$SOCK" > "$d/handover.json" 2>&1
  (
    while :; do
      printf '%s,%s\n' "$(date +%s%3N)" \
        "$("$EP" status --socket "$SOCK" 2>/dev/null | python3 -c '
import json, sys
doc = json.load(sys.stdin)
a, s = doc["auto_apply"], doc["service"]
print("%s;%s;%s;%s;%s;%s" % (a.get("active_expert") or "-", a.get("switches_recorded"),
                             (a.get("last_hold") or "-").replace(",", " "),
                             s.get("foreground_p99_ms"), s.get("bg_work_units_per_sec"),
                             s.get("age_ms")))')" >> "$d/status.csv"
      sleep 2
    done
  ) 9>&- &
  POLL_PID=$!
  echo "  [$rep/$arm] measuring ${MEASURE_S}s with the gate in play"
  "$PROBE" "$CONNS" "$RATE" "$MEASURE_S" GET --port "$PORT" --keypool "$KEYPOOL" \
    --slo-ms 5 --timeout-ms 50 --raw "$d/raw-inject.bin" > "$d/inject.json" 2>>"$d/probe.err"
  "$EP" status --socket "$SOCK" > "$d/status-inject.json" 2>&1

  # Phase 4: drop the load and let it recover.
  for pid in "${LOAD_PIDS[@]:-}"; do kill -KILL "$pid" 2>/dev/null; wait "$pid" 2>/dev/null; done
  LOAD_PIDS=()
  echo "  [$rep/$arm] recovery ${RECOVER_S}s"
  "$PROBE" "$CONNS" "$RATE" "$RECOVER_S" GET --port "$PORT" --keypool "$KEYPOOL" \
    --slo-ms 5 --timeout-ms 50 --raw "$d/raw-recover.bin" > "$d/recover.json" 2>>"$d/probe.err"
  "$EP" status --socket "$SOCK" > "$d/status-recover.json" 2>&1

  kill -KILL "$POLL_PID" 2>/dev/null; wait "$POLL_PID" 2>/dev/null; POLL_PID=""
  stop_all
  (cd "$d" && sha256sum ./* 2>/dev/null | grep -v SHA256SUMS > SHA256SUMS)
  echo "  [$rep/$arm] done"
}

stop_all() {
  for pid in "$OBS_PID" "$BG_PID"; do
    [ -n "${pid:-}" ] && kill -KILL "$pid" 2>/dev/null; wait "$pid" 2>/dev/null
  done
  OBS_PID=""; BG_PID=""
  if [ -n "$DAEMON" ] && kill -0 "$DAEMON" 2>/dev/null; then
    kill -TERM "$DAEMON" 2>/dev/null; wait "$DAEMON" 2>/dev/null
  fi
  DAEMON=""
  if [ -n "$REDIS_PID" ] && kill -0 "$REDIS_PID" 2>/dev/null; then
    redis-cli -p "$PORT" shutdown nosave >/dev/null 2>&1
    kill -TERM "$REDIS_PID" 2>/dev/null; wait "$REDIS_PID" 2>/dev/null
  fi
  REDIS_PID=""
  for _ in $(seq 1 60); do
    [ "$(cat /sys/kernel/sched_ext/state 2>/dev/null)" = "disabled" ] && break
    sleep 0.5
  done
}

for rep in $(seq 1 "$REPEATS"); do
  # Alternate the order: D always first would confound the arm with whatever drift the
  # box has between the two runs of a repeat.
  if [ $(( rep % 2 )) -eq 0 ]; then order="Dng D"; else order="D Dng"; fi
  for arm in $order; do
    run_arm "$rep" "$arm" || echo "repeat $rep arm $arm failed"
  done
done

python3 - "$OUT" <<'REPORT'
import csv, glob, json, os, statistics as st, sys
root = sys.argv[1]
rows = []
for d in sorted(glob.glob(os.path.join(root, "r*-*"))):
    if not os.path.isdir(d) or os.path.exists(os.path.join(d, "block.invalid")):
        continue
    rep, arm = os.path.basename(d)[1:].split("-", 1)
    rec = {"repeat": int(rep), "arm": arm}
    for phase in ("baseline", "inject", "recover"):
        path = os.path.join(d, "%s.json" % phase)
        if os.path.exists(path):
            doc = json.load(open(path))
            rec[phase] = {"goodput": doc.get("goodput_at_slo_rps"),
                          "achieved": doc.get("achieved_rps"),
                          "timeouts": doc.get("timeouts")}
    polls = []
    spath = os.path.join(d, "status.csv")
    if os.path.exists(spath):
        for line in open(spath):
            parts = line.strip().split(",")
            if len(parts) >= 2:
                fields = parts[1].split(";")
                if len(fields) >= 6:
                    polls.append(fields)
    rec["polls"] = len(polls)
    rec["holds"] = sorted({p[2] for p in polls})
    rec["denied"] = sum(1 for p in polls if "slo denied" in p[2])
    rec["switches_seen"] = sorted({p[1] for p in polls})
    rec["max_p99"] = max((float(p[3]) for p in polls if p[3] not in ("None", "-")), default=None)
    rec["min_bg"] = min((float(p[4]) for p in polls if p[4] not in ("None", "-")), default=None)
    rows.append(rec)

print("runs: %d" % len(rows))
for rec in rows:
    print("r%-2d %-4s goodput baseline/inject/recover %s/%s/%s | polls %d | denied %d | "
          "switches %s | max p99 %s | min bg %s" % (
              rec["repeat"], rec["arm"],
              rec.get("baseline", {}).get("goodput"), rec.get("inject", {}).get("goodput"),
              rec.get("recover", {}).get("goodput"), rec["polls"], rec["denied"],
              ",".join(rec["switches_seen"]), rec["max_p99"], rec["min_bg"]))

def paired(arm_series, base_series):
    pairs = []
    for key in sorted(set(arm_series) & set(base_series)):
        a, b = arm_series[key], base_series[key]
        if a and b:
            pairs.append(100.0 * (a / b - 1.0))
    return pairs

by_arm = {}
for r in rows:
    by_arm.setdefault(r["arm"], {})[r["repeat"]] = r.get("inject", {}).get("goodput")
# The premise checks are the point of this driver: without them a null would be
# indistinguishable from "the condition never existed".
for arm in sorted(by_arm):
    denied = [r["denied"] for r in rows if r["arm"] == arm]
    switches = [r["switches_seen"] for r in rows if r["arm"] == arm]
    if arm == "D":
        ok = all(d > 0 for d in denied)
        print("CHECK_GATE_FIRED_IN_D=%s (denials per run: %s)" % ("yes" if ok else "no", denied))
    else:
        moved = all(any(int(x) >= 1 for x in s) for s in switches if s)
        print("CHECK_DNG_COULD_ACT=%s (switch counters seen: %s)" % ("yes" if moved else "no", switches))
if "D" in by_arm and "Dng" in by_arm:
    pairs = paired(by_arm["Dng"], by_arm["D"])
    if pairs:
        print("Dng vs D inject-phase goodput: median %+.2f%% (per repeat: %s)" % (
            st.median(pairs), ", ".join("%+.2f%%" % p for p in pairs)))
REPORT
