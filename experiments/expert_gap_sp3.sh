#!/usr/bin/env bash
# EulerPilot v2 -- per-second value of the expert the gate delays you from reaching.
#
# The gate run measured the delay deterministically (D held ~40s while Dng switched at
# ~12s) but could not measure what that delay cost, because goodput inside the injected
# phase swings 85-93% within the same arm. The noise is in the machine's equilibrium,
# not in the estimator, so more repeats of the *same* design would not help.
#
# This driver measures the other factor of the product instead:
#
#     cost_of_delay = (metric gap between the two experts under the condition) x delay
#
# Both experts are applied statically and allowed to settle *before* the measurement
# starts, so no switch happens inside the measured window and no autopilot is involved.
# That is the only comparison here that can be made without fighting the transient.
# The delay factor comes from evidence/p2/gate-injection-20261023.txt (~40s).
#
#   expert_gap_sp3.sh <ep-executord> <ep> <ep-sched-loader> [out-dir] [repeats] [rate]
#
# Needs root, idle sched_ext, redis-server, built probes. Manual, not part of ctest.
set -uo pipefail

EXECUTORD="${1:?usage: expert_gap_sp3.sh <ep-executord> <ep> <ep-sched-loader> [out-dir] [repeats] [rate]}"
EP="${2:?missing ep}"
LOADER="${3:?missing ep-sched-loader}"
OUT="${4:-/root/epv2-evidence/expert-gap}"
REPEATS="${5:-4}"
RATE="${6:-60000}"
HERE="$(cd "$(dirname "$0")" && pwd)"
CTL="$(dirname "$LOADER")/ep-schedctl"
PROBE="$HERE/redis_openloop_probe"
SVC="$HERE/service_load_probe"
PIN_ROOT=/sys/fs/bpf/eulerpilot-v2
CONNS=8
KEYPOOL=100000
PORT="${PORT:-7805}"
BG_WINDOW_S=5
SETTLE_S="${SETTLE_S:-10}"
MEASURE_S="${MEASURE_S:-60}"
SPINNERS="${SPINNERS:-16}"
EXPERTS="${EXPERTS:-safe-basic latency-balanced}"
RUN=/tmp/ep-expert-gap
CG_SVC=/sys/fs/cgroup/gap-svc
CG_BG=/sys/fs/cgroup/gap-bg
SOCK="$RUN/executor.sock"

[ "$(id -u)" = 0 ] || { echo "requires root"; exit 77; }
[ "$(cat /sys/kernel/sched_ext/state 2>/dev/null)" = "disabled" ] || { echo "sched_ext not idle"; exit 1; }
[ -z "$(pgrep -x ep-executord)" ] || { echo "executor already running"; exit 1; }
for need in "$PROBE" "$SVC"; do
  [ -x "$need" ] || { echo "missing $need"; exit 1; }
done

mkdir -p "$OUT" "$CG_SVC" "$CG_BG"
DAEMON=""; REDIS_PID=""; BG_PID=""
LOAD_PIDS=()

cleanup() {
  local rc=$?
  for pid in "$BG_PID" "${LOAD_PIDS[@]:-}"; do
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
  if [ "$(cat /sys/kernel/sched_ext/state 2>/dev/null)" = "disabled" ] &&
     [ -z "$(pgrep -x ep-executord)" ]; then
    for dir in "$PIN_ROOT"/inst-*; do [ -d "$dir" ] && rm -rf "$dir"; done
  fi
  rmdir "$PIN_ROOT" "$CG_SVC" "$CG_BG" 2>/dev/null
  echo "cleanup rc=$rc state=$(cat /sys/kernel/sched_ext/state 2>/dev/null) pins=$(ls -1 "$PIN_ROOT" 2>/dev/null | wc -l)"
}
trap cleanup EXIT

policy_json() {  # file expert
  python3 - "$1" "$2" "$CG_SVC" "$CG_BG" <<'POLICYGEN'
import json, sys
path, expert, svc_cg, bg_cg = sys.argv[1:5]
json.dump({
  "apiVersion": "eulerpilot.io/v1alpha1", "kind": "SchedulingPolicy",
  "metadata": {"name": "expert-gap-%s" % expert},
  "spec": {
    "scope": "scheduler-instance", "mode": "class", "expert": expert,
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
               "bg_progress_window_ms": 5000, "min_bg_work_units_per_sec": 140,
               "max_bg_wait_ms": 50, "max_foreground_p99_ms": 10,
               "fallback": "verified-safe-profile", "allow_affinity_change": False}
  }
}, open(path, "w"))
POLICYGEN
}

stop_all() {
  for pid in "$BG_PID"; do [ -n "${pid:-}" ] && kill -KILL "$pid" 2>/dev/null; wait "$pid" 2>/dev/null; done
  BG_PID=""
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

run_one() {  # repeat expert
  local rep=$1 expert=$2 d="$OUT/r$1-$2"
  mkdir -p "$d"; rm -rf "$RUN/state"; mkdir -p "$RUN/state"
  policy_json "$d/policy.json" "$expert"

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
    --loader "$LOADER" --sched-ctl "$CTL" >"$d/daemon.log" 2>&1 &
  DAEMON=$!
  for _ in $(seq 1 100); do "$EP" status --socket "$SOCK" >/dev/null 2>&1 && break; sleep 0.1; done
  "$EP" apply --file "$d/policy.json" --idempotency-key "gap-$rep-$expert" --ttl-ms 900000 \
    --socket "$SOCK" >"$d/apply.json" 2>&1
  local status
  status=$(python3 -c "import json;print(json.load(open('$d/apply.json')).get('status'))")
  if [ "$status" != "VERIFIED" ]; then
    echo "$expert: apply returned $status" >> "$d/block.invalid"; stop_all; return 1
  fi
  (
    echo "$BASHPID" > "$CG_BG/cgroup.procs"
    while :; do "$SVC" bg safe-basic "$BG_WINDOW_S"; done
  ) >>"$d/bg.ndjson" 2>>"$d/bg.err" 9>&- &
  BG_PID=$!

  for _ in $(seq 1 "$SPINNERS"); do
    (while :; do :; done) </dev/null >/dev/null 2>&1 9>&- &
    LOAD_PIDS+=("$!")
  done
  echo "  [$rep/$expert] injected ${SPINNERS} spinners, settling ${SETTLE_S}s"
  sleep "$SETTLE_S"
  "$EP" status --socket "$SOCK" > "$d/status-before.json" 2>&1
  # Premise: the expert that is actually in place is the one this run claims. Without
  # this the comparison could silently be "same expert twice".
  local active
  active=$(python3 -c "import json;print(json.load(open('$d/status-before.json'))['auto_apply'].get('active_expert') or '')")
  if [ "$active" != "$expert" ]; then
    echo "$expert: active expert is '$active'" >> "$d/block.invalid"
    stop_all
    return 1
  fi
  echo "  [$rep/$expert] measuring ${MEASURE_S}s"
  "$PROBE" "$CONNS" "$RATE" "$MEASURE_S" GET --port "$PORT" --keypool "$KEYPOOL" \
    --slo-ms 5 --timeout-ms 50 --raw "$d/raw-inject.bin" > "$d/inject.json" 2>>"$d/probe.err"
  "$EP" status --socket "$SOCK" > "$d/status-after.json" 2>&1
  for pid in "${LOAD_PIDS[@]:-}"; do kill -KILL "$pid" 2>/dev/null; wait "$pid" 2>/dev/null; done
  LOAD_PIDS=()
  stop_all
  (cd "$d" && sha256sum ./* 2>/dev/null | grep -v SHA256SUMS > SHA256SUMS)
  echo "  [$rep/$expert] done"
}

for rep in $(seq 1 "$REPEATS"); do
  # Alternate the order so a drift in the box cannot masquerade as an expert effect.
  if [ $(( rep % 2 )) -eq 0 ]; then
    order="$EXPERTS"
  else
    order=""; for e in $EXPERTS; do order="$e $order"; done
  fi
  for expert in $order; do
    run_one "$rep" "$expert" || echo "repeat $rep expert $expert failed"
  done
done

python3 - "$OUT" <<'REPORT'
import glob, json, os, random, statistics as st, struct, sys
root = sys.argv[1]

def pct(sorted_vals, q):
    idx = int(-(-q * len(sorted_vals) // 100)) - 1
    return sorted_vals[min(max(idx, 0), len(sorted_vals) - 1)]

rows = []
for d in sorted(glob.glob(os.path.join(root, "r*-*"))):
    if not os.path.isdir(d) or os.path.exists(os.path.join(d, "block.invalid")):
        continue
    rep, expert = os.path.basename(d)[1:].split("-", 1)
    raw = os.path.join(d, "raw-inject.bin")
    if not os.path.exists(raw):
        continue
    blob = open(raw, "rb").read()
    vals = sorted(struct.unpack("<%dQ" % (len(blob) // 8), blob))
    doc = json.load(open(os.path.join(d, "inject.json")))
    elapsed = (doc["finished_at_ms"] - doc["offered_at_ms"]) / 1000.0
    over5 = sum(1 for v in vals if v > 5e6)
    rows.append({"rep": int(rep), "expert": expert, "n": len(vals),
                 "goodput": sum(1 for v in vals if v <= 5e6) / elapsed,
                 "mean_ms": st.mean(vals) / 1e6,
                 "p50": pct(vals, 50) / 1e6, "p99": pct(vals, 99) / 1e6,
                 "p999": pct(vals, 99.9) / 1e6,
                 "over5_pct": 100.0 * over5 / len(vals),
                 "inflight": doc.get("peak_inflight"), "timeouts": doc.get("timeouts")})

experts = sorted({r["expert"] for r in rows})
print("runs: %d | experts: %s" % (len(rows), experts))
for r in rows:
    print("r%-2d %-16s goodput %7.0f  mean %5.2f  p50 %5.2f  p99 %6.2f  p99.9 %6.2f  over5%% %5.2f  inflight %s"
          % (r["rep"], r["expert"], r["goodput"], r["mean_ms"], r["p50"], r["p99"], r["p999"],
             r["over5_pct"], r["inflight"]))

A_EXPERT, B_EXPERT = "safe-basic", "latency-balanced"

def paired(a_key, b_key):
    """Percent difference of the latency-balanced expert against safe-basic,
    paired by repeat. Negative means latency-balanced is *worse* for higher-is-better
    metrics and *better* for p99/mean."""
    A = {r["rep"]: r[a_key] for r in rows if r["expert"] == A_EXPERT}
    B = {r["rep"]: r[b_key] for r in rows if r["expert"] == B_EXPERT}
    return [100.0 * (B[k] / A[k] - 1.0) for k in sorted(set(A) & set(B)) if A[k]]

def boot(pairs, n=10000, seed=20261024):
    if not pairs:
        return None
    rng = random.Random(seed)
    meds = sorted(st.median([pairs[rng.randrange(len(pairs))] for _ in pairs]) for _ in range(n))
    return st.median(pairs), meds[int(0.025 * len(meds))], meds[min(int(0.975 * len(meds)), len(meds) - 1)]

if {r["expert"] for r in rows} >= {A_EXPERT, B_EXPERT}:
    for metric, label in (("goodput", "goodput@5ms"), ("p99", "p99"), ("p999", "p99.9"),
                          ("mean_ms", "mean latency"), ("over5_pct", "share over 5ms")):
        pairs = paired(metric, metric)
        b = boot(pairs)
        if b:
            print("latency-balanced vs safe-basic %-18s median %+.2f%% [%.2f, %.2f] "
                  "(per repeat: %s)" % (label, b[0], b[1], b[2],
                                        ", ".join("%+.2f%%" % p for p in pairs)))
    print()
    gap = boot(paired("goodput", "goodput"))
    if gap:
        base = st.median([r["goodput"] for r in rows if r["expert"] == A_EXPERT])
        per_s = base * gap[0] / 100.0
        # A 1- or 2-sample bootstrap has a zero-width CI, which would read as a resolved
        # gap. Require enough repeats before calling anything resolved.
        resolved = len(paired("goodput", "goodput")) >= 3 and (gap[1] > 0 or gap[2] < 0)
        print("GAP_RESOLVED=%s (n=%d, CI [%.2f, %.2f])" % (
            "yes" if resolved else "no", len(paired("goodput", "goodput")), gap[1], gap[2]))
        print("IMPLIED_COST_OF_DELAY_40S=%+.0f requests (%.0f rps x %+.2f%% x 40s); "
              "quote it only if GAP_RESOLVED=yes" % (per_s * 40.0, base, gap[0]))
REPORT
