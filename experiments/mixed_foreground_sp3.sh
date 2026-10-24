#!/usr/bin/env bash
# EulerPilot v2 -- mixed-foreground: two foreground services with conflicting SLOs.
#
# Protocol §4 asks for two foreground services competing, judged jointly. Every earlier
# run served one workload, so the class whitelist was only ever asked to separate
# foreground from background -- not to arbitrate between two foregrounds that want
# different things. Here two redis instances run side by side with different offered
# rates and different SLOs, each in its own declared cgroup:
#
#   svc-a  30k rps, SLO 5ms, class LAT       (the latency-sensitive one)
#   svc-b  15k rps, SLO 2ms, class COMPUTE   (the bulk one, but with the tighter budget)
#
# Arms: A0 = bare services on the kernel scheduler (no executor);
#       D  = executor + autopilot, both services declared, lease handed over.
# Judgement: per-service goodput@SLO and p99, plus "joint SLO" = both services meet
# their budget in the run. A service that meets its SLO alone but not alongside the
# other is the interesting failure, so both are always reported.
#
#   mixed_foreground_sp3.sh <ep-executord> <ep> <ep-sched-loader> [out-dir] [repeats]
#
# Needs root, idle sched_ext, redis-server, built probes. Manual, not part of ctest.
set -uo pipefail

EXECUTORD="${1:?usage: mixed_foreground_sp3.sh <ep-executord> <ep> <ep-sched-loader> [out-dir] [repeats]}"
EP="${2:?missing ep}"
LOADER="${3:?missing ep-sched-loader}"
OUT="${4:-/root/epv2-evidence/mixed-foreground}"
REPEATS="${5:-3}"
HERE="$(cd "$(dirname "$0")" && pwd)"
CTL="$(dirname "$LOADER")/ep-schedctl"
PROBE="$HERE/redis_openloop_probe"
SVC="$HERE/service_load_probe"
OBSERVER="$HERE/service_observer_sp3.sh"
PIN_ROOT=/sys/fs/bpf/eulerpilot-v2
CONNS=8
KEYPOOL=100000
PORT_A="${PORT_A:-7811}"
PORT_B="${PORT_B:-7812}"
RATE_A="${RATE_A:-30000}"
RATE_B="${RATE_B:-15000}"
SLO_A="${SLO_A:-5}"
SLO_B="${SLO_B:-2}"
MEASURE_S="${MEASURE_S:-60}"
BG_WINDOW_S=5
# Two foregrounds only compete for something scarce. With the box idle both meet their
# SLOs trivially, which tests nothing; the noisy neighbour is what makes the joint
# judgement meaningful (and the LAT/COMPUTE split is only a claim under contention).
SPINNERS="${SPINNERS:-8}"
SPINNER_NICE="${SPINNER_NICE:-0}"
RUN=/tmp/ep-mixed-fg
CG_A=/sys/fs/cgroup/mf-a
CG_B=/sys/fs/cgroup/mf-b
CG_BG=/sys/fs/cgroup/mf-bg
SOCK="$RUN/executor.sock"
DAEMON=""; REDIS_A=""; REDIS_B=""; BG_PID=""; OBS_PID=""
LOAD_PIDS=()

[ "$(id -u)" = 0 ] || { echo "requires root"; exit 77; }
[ "$(cat /sys/kernel/sched_ext/state 2>/dev/null)" = "disabled" ] || { echo "sched_ext not idle"; exit 1; }
[ -z "$(pgrep -x ep-executord)" ] || { echo "executor already running"; exit 1; }
for need in "$PROBE" "$SVC" "$OBSERVER"; do
  [ -x "$need" ] || { echo "missing $need"; exit 1; }
done
[ "$(redis-server --version | grep -c 'v=7')" = "1" ] || echo "warning: unexpected redis version"

mkdir -p "$OUT" "$CG_A" "$CG_B" "$CG_BG"

cleanup() {
  local rc=$?
  for pid in "$OBS_PID" "$BG_PID" "${LOAD_PIDS[@]:-}"; do
    [ -n "${pid:-}" ] && kill -KILL "$pid" 2>/dev/null; wait "$pid" 2>/dev/null
  done
  LOAD_PIDS=()
  if [ -n "$DAEMON" ] && kill -0 "$DAEMON" 2>/dev/null; then
    kill -TERM "$DAEMON" 2>/dev/null; wait "$DAEMON" 2>/dev/null
  fi
  for port in "$PORT_A" "$PORT_B"; do redis-cli -p "$port" shutdown nosave >/dev/null 2>&1; done
  for pid in "$REDIS_A" "$REDIS_B"; do
    [ -n "${pid:-}" ] && kill -TERM "$pid" 2>/dev/null; wait "$pid" 2>/dev/null
  done
  for _ in $(seq 1 60); do
    [ "$(cat /sys/kernel/sched_ext/state 2>/dev/null)" = "disabled" ] && break
    sleep 0.5
  done
  if [ "$(cat /sys/kernel/sched_ext/state 2>/dev/null)" = "disabled" ] &&
     [ -z "$(pgrep -x ep-executord)" ]; then
    for dir in "$PIN_ROOT"/inst-*; do [ -d "$dir" ] && rm -rf "$dir"; done
  fi
  rmdir "$PIN_ROOT" "$CG_A" "$CG_B" "$CG_BG" 2>/dev/null
  echo "cleanup rc=$rc state=$(cat /sys/kernel/sched_ext/state 2>/dev/null) pins=$(ls -1 "$PIN_ROOT" 2>/dev/null | wc -l)"
}
trap cleanup EXIT

policy_json() {  # file
  python3 - "$1" <<'POLICYGEN'
import json, sys
json.dump({
  "apiVersion": "eulerpilot.io/v1alpha1", "kind": "SchedulingPolicy",
  "metadata": {"name": "mixed-fg"},
  "spec": {
    "scope": "scheduler-instance", "mode": "class", "expert": "safe-basic", "source": "human",
    "targets": [
      {"kind": "cgroup", "path": "/sys/fs/cgroup/mf-a", "role": "FOREGROUND",
       "role_source": "explicit", "class": "LAT"},
      {"kind": "cgroup", "path": "/sys/fs/cgroup/mf-b", "role": "FOREGROUND",
       "role_source": "explicit", "class": "COMPUTE"},
      {"kind": "cgroup", "path": "/sys/fs/cgroup/mf-bg", "role": "BACKGROUND",
       "role_source": "explicit"}
    ],
    "parameters": {"lat_slice_ns": 1000000, "comp_slice_ns": 4000000,
                   "cache_slice_ns": 4000000, "bg_slice_ns": 2000000,
                   "preempt_threshold_ns": 500000, "bg_vtime_pct": 200,
                   "llc_affinity": False},
    "safety": {"min_dwell_ms": 5000, "max_observation_age_ms": 1000,
               "heartbeat_period_ms": 250, "heartbeat_timeout_ms": 2000,
               "policy_ttl_ms": 900000, "quiet_max_regression_pct": 5,
               "evaluation_min_bg_progress_ratio": 0.5, "bg_work_unit": "probe5s",
               "bg_progress_window_ms": 5000, "min_bg_work_units_per_sec": 140,
               "max_bg_wait_ms": 50, "max_foreground_p99_ms": 10,
               "fallback": "verified-safe-profile", "allow_affinity_change": False}
  }
}, open(sys.argv[1], "w"))
POLICYGEN
}

start_redis() {  # port cgroup logprefix
  local port=$1 cg=$2 log=$3 pid
  redis-server --port "$port" --daemonize no --save '' --appendonly no --io-threads 1 \
    --maxmemory 512mb --logfile "$log" >"$log.stdout" 2>&1 9>&- &
  pid=$!
  echo "$pid" > "$cg/cgroup.procs"
  for _ in $(seq 1 100); do redis-cli -p "$port" ping >/dev/null 2>&1 && break; sleep 0.2; done
  python3 - "$KEYPOOL" > "$RUN/populate-$port.redis" <<'GEN'
import sys
n = int(sys.argv[1])
out = sys.stdout.buffer
for i in range(n):
    key = ("k%d" % (i % n)).encode()
    out.write(b"*3\r\n$3\r\nSET\r\n$%d\r\n%s\r\n$64\r\n%s\r\n" % (len(key), key, b"a" * 64))
GEN
  redis-cli -p "$port" --pipe < "$RUN/populate-$port.redis" > "$log.populate" 2>&1
  echo "$pid"
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
  for port in "$PORT_A" "$PORT_B"; do redis-cli -p "$port" shutdown nosave >/dev/null 2>&1; done
  for pid in "$REDIS_A" "$REDIS_B"; do
    [ -n "${pid:-}" ] && kill -TERM "$pid" 2>/dev/null; wait "$pid" 2>/dev/null
  done
  REDIS_A=""; REDIS_B=""
  for _ in $(seq 1 60); do
    [ "$(cat /sys/kernel/sched_ext/state 2>/dev/null)" = "disabled" ] && break
    sleep 0.5
  done
}

run_arm() {  # repeat arm
  local rep=$1 arm=$2 d="$OUT/r$1-$2"
  mkdir -p "$d"; rm -rf "$RUN/state"; mkdir -p "$RUN/state"
  REDIS_A=$(start_redis "$PORT_A" "$CG_A" "$d/redis-a.log")
  REDIS_B=$(start_redis "$PORT_B" "$CG_B" "$d/redis-b.log")

  if [ "$arm" = "A0" ]; then
    echo "  [$rep/A0] bare services on the kernel scheduler (no executor)"
  else
    policy_json "$d/policy.json"
    "$EXECUTORD" --state-dir "$RUN/state" --socket "$SOCK" --skill sched \
      --loader "$LOADER" --sched-ctl "$CTL" \
      --auto-apply --auto-policy "$d/policy.json" --auto-owner autopilot-v2 \
      --auto-min-interval-ms 3000 --auto-lease-ttl-ms 900000 \
      --auto-dwell-ms 20000 --auto-candidate-hold-ms 10000 \
      --auto-budget-window-ms 900000 --auto-budget-max-switches 8 \
      >"$d/daemon.log" 2>&1 &
    DAEMON=$!
    for _ in $(seq 1 100); do "$EP" status --socket "$SOCK" >/dev/null 2>&1 && break; sleep 0.1; done
    "$EP" apply --file "$d/policy.json" --idempotency-key "mfg-apply-$rep-$arm" --ttl-ms 900000 \
      --socket "$SOCK" >"$d/apply.json" 2>&1
    local status
    status=$(python3 -c "import json;print(json.load(open('$d/apply.json')).get('status'))")
    if [ "$status" != "VERIFIED" ]; then
      echo "$arm: apply returned $status" >> "$d/block.invalid"; stop_all; return 1
    fi
    python3 - "$d/apply.json" "$d/classes.proof" <<'CLASSPROOF'
import json, sys
reply = json.load(open(sys.argv[1]))
got = sorted({(b.get("class") or "", b.get("effective_class") or "")
              for b in (reply.get("target_bindings") or [])})
want = [("", "BG"), ("COMPUTE", "COMPUTE"), ("LAT", "LAT")]
open(sys.argv[2], "w").write("declared_effective=%s expected=%s ok=%s\n"
                             % (got, want, "yes" if got == want else "no"))
raise SystemExit(0 if got == want else 1)
CLASSPROOF
    if [ "$?" != "0" ]; then
      echo "$arm: class declarations did not land (see classes.proof)" >> "$d/block.invalid"
      stop_all
      return 1
    fi
    (
      bash "$OBSERVER" "$EP" "$SOCK" "$SVC" "$d/observer" 5 1 400 >/dev/null 2>&1
    ) 9>&- &
    OBS_PID=$!
    "$EP" lease --owner autopilot-v2 --ttl-ms 900000 --socket "$SOCK" > "$d/handover.json" 2>&1
  fi

  (
    echo "$BASHPID" > "$CG_BG/cgroup.procs"
    while :; do "$SVC" bg safe-basic "$BG_WINDOW_S"; done
  ) >>"$d/bg.ndjson" 2>>"$d/bg.err" 9>&- &
  BG_PID=$!
  sleep 3
  for _ in $(seq 1 "$SPINNERS"); do
    if [ "$SPINNER_NICE" != "0" ]; then
      nice -n "$SPINNER_NICE" bash -c 'while :; do :; done' </dev/null >/dev/null 2>&1 9>&- &
    else
      (while :; do :; done) </dev/null >/dev/null 2>&1 9>&- &
    fi
    LOAD_PIDS+=("$!")
  done

  echo "  [$rep/$arm] both services + ${SPINNERS} spinners, ${MEASURE_S}s (A ${RATE_A}rps/${SLO_A}ms, B ${RATE_B}rps/${SLO_B}ms)"
  "$PROBE" "$CONNS" "$RATE_A" "$MEASURE_S" GET --port "$PORT_A" --keypool "$KEYPOOL" \
    --slo-ms "$SLO_A" --timeout-ms 50 --raw "$d/raw-a.bin" > "$d/a.json" 2>>"$d/probe-a.err" &
  local pa=$!
  "$PROBE" "$CONNS" "$RATE_B" "$MEASURE_S" GET --port "$PORT_B" --keypool "$KEYPOOL" \
    --slo-ms "$SLO_B" --timeout-ms 50 --raw "$d/raw-b.bin" > "$d/b.json" 2>>"$d/probe-b.err" &
  local pb=$!
  wait "$pa" "$pb"
  for pid in "${LOAD_PIDS[@]:-}"; do kill -KILL "$pid" 2>/dev/null; wait "$pid" 2>/dev/null; done
  LOAD_PIDS=()
  [ -n "$DAEMON" ] && "$EP" status --socket "$SOCK" > "$d/status.json" 2>&1
  stop_all
  (cd "$d" && sha256sum ./* 2>/dev/null | grep -v SHA256SUMS > SHA256SUMS)
  echo "  [$rep/$arm] done"
}

for rep in $(seq 1 "$REPEATS"); do
  for arm in A0 D; do run_arm "$rep" "$arm" || echo "repeat $rep arm $arm failed"; done
done

python3 - "$OUT" <<'REPORT'
import glob, json, os, statistics as st, struct, sys
root = sys.argv[1]
SERVICES = {"a": ("30k/5ms", 5.0), "b": ("15k/2ms", 2.0)}


def pct(vals, q):
    idx = int(-(-q * len(vals) // 100)) - 1
    return vals[min(max(idx, 0), len(vals) - 1)]


rows = []
for d in sorted(glob.glob(os.path.join(root, "r*-*"))):
    if not os.path.isdir(d) or os.path.exists(os.path.join(d, "block.invalid")):
        continue
    rep, arm = os.path.basename(d)[1:].split("-", 1)
    rec = {"rep": int(rep), "arm": arm, "svc": {}}
    for key, (label, slo) in SERVICES.items():
        raw, doc_path = os.path.join(d, "raw-%s.bin" % key), os.path.join(d, "%s.json" % key)
        if not (os.path.exists(raw) and os.path.exists(doc_path)):
            continue
        doc = json.load(open(doc_path))
        blob = open(raw, "rb").read()
        vals = sorted(struct.unpack("<%dQ" % (len(blob) // 8), blob))
        if not vals:
            continue
        elapsed = (doc["finished_at_ms"] - doc["offered_at_ms"]) / 1000.0
        rec["svc"][key] = {
            "label": label, "slo": slo, "goodput": sum(1 for v in vals if v <= slo * 1e6) / elapsed,
            "met": 100.0 * sum(1 for v in vals if v <= slo * 1e6) / len(vals) >= 99.0,
            "p99": pct(vals, 99) / 1e6, "p50": pct(vals, 50) / 1e6,
            "inflight": doc.get("peak_inflight")}
    rec["joint"] = all(rec["svc"][k]["met"] for k in SERVICES if k in rec["svc"]) and len(rec["svc"]) == 2
    rows.append(rec)

print("runs: %d" % len(rows))
for rec in rows:
    parts = []
    for key in ("a", "b"):
        v = rec["svc"].get(key)
        parts.append("%s gp %7.0f p99 %6.2f %s" % (
            key.upper(), v["goodput"], v["p99"], "ok" if v["met"] else "MISS") if v else "%s -" % key.upper())
    print("r%-2d %-3s %s joint=%s" % (rec["rep"], rec["arm"], " | ".join(parts),
                                      "yes" if rec["joint"] else "no"))

print()
for arm in sorted({r["arm"] for r in rows}):
    subset = [r for r in rows if r["arm"] == arm]
    for key in ("a", "b"):
        vals = [r["svc"][key] for r in subset if key in r["svc"]]
        if not vals:
            continue
        print("%-3s svc-%s (%s): goodput %7.0f  p99 %6.2f  met %d/%d  inflight max %s" % (
            arm, key.upper(), vals[0]["label"], st.median(v["goodput"] for v in vals),
            st.median(v["p99"] for v in vals), sum(1 for v in vals if v["met"]), len(vals),
            max(v["inflight"] for v in vals)))
    print("%-3s joint SLO met in %d/%d runs" % (arm, sum(1 for r in subset if r["joint"]), len(subset)))
REPORT
