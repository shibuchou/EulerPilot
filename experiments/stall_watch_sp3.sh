#!/usr/bin/env bash
# EulerPilot v2 -- does the data plane survive sustained load?
#
# The flagship pilot found that every attached block lost the scheduler to the kernel
# (dmesg: `BPF scheduler "eulerpilot" disabled (runnable task stall)` with a per-CPU
# kworker as the victim, ~45s after each enable). The cause is that ops.enqueue() is
# not preceded by ops.select_cpu() for re-enqueues and migrations, and select_cpu is
# the only thing that wakes an idle target CPU -- so a task queued on a CPU that is
# idle and has no other reason to balance stays queued until the 30s watchdog
# unregisters the scheduler. The fix kicks an idle allowed CPU on non-wakeup enqueues
# (sched/scx_schedpilot.bpf.c:kick_idle_for).
#
# This script is the verification, and it is deliberately about survival rather than
# performance: attach, run the same load that reproduced the stall five times out of
# five, poll sched_ext every second, and report whether anything died.
#
#   stall_watch_sp3.sh <ep-executord> <ep> <ep-sched-loader> [out-dir] [rounds] [seconds]
#
# Needs root, an idle sched_ext, redis-server, and the built probes next to this script.
set -uo pipefail

EXECUTORD="${1:?usage: stall_watch_sp3.sh <ep-executord> <ep> <ep-sched-loader> [out-dir] [rounds] [seconds]}"
EP="${2:?missing ep}"
LOADER="${3:?missing ep-sched-loader}"
OUT="${4:-/root/epv2-evidence/stall-watch}"
ROUNDS="${5:-3}"
ROUND_S="${6:-60}"
HERE="$(cd "$(dirname "$0")" && pwd)"
CTL="$(dirname "$LOADER")/ep-schedctl"
PROBE="$HERE/redis_openloop_probe"
SVC="$HERE/service_load_probe"
PIN_ROOT=/sys/fs/bpf/eulerpilot-v2
RATE="${RATE:-60000}"
CONNS="${CONNS:-8}"
KEYPOOL="${KEYPOOL:-100000}"
PORT="${PORT:-7799}"
BG_WINDOW_S="${BG_WINDOW_S:-5}"
# Ingredient triage: the same attach can be tested with the service load only, the
# background load only, neither, or both. Which combination is required narrows the
# defect far faster than reading more of the scheduler does.
LOAD_REDIS="${LOAD_REDIS:-1}"
LOAD_BG="${LOAD_BG:-1}"
RUN=/tmp/ep-stall-watch

[ "$(id -u)" = 0 ] || { echo "requires root"; exit 77; }
[ "$(cat /sys/kernel/sched_ext/state 2>/dev/null)" = "disabled" ] || { echo "sched_ext not idle"; exit 1; }
[ -z "$(pgrep -x ep-executord)" ] || { echo "executor already running"; exit 1; }
for need in "$PROBE" "$SVC"; do
  [ -x "$need" ] || { echo "missing $need (compile experiments/*.c)"; exit 1; }
done

mkdir -p "$OUT"
rm -rf "$RUN"; mkdir -p "$RUN/state"
SOCK="$RUN/executor.sock"; BASELINE="$RUN/baseline.json"
DAEMON=""; REDIS_PID=""; BG_PID=""; WATCH_PID=""
: > "$OUT/state.polls.csv"
dmesg_before=$(dmesg | wc -l)

cleanup() {
  local rc=$?
  for pid in "$WATCH_PID" "$BG_PID" "$DAEMON"; do
    [ -n "$pid" ] && kill -KILL "$pid" 2>/dev/null; wait "$pid" 2>/dev/null
  done
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
  rmdir "$PIN_ROOT" 2>/dev/null
  echo "cleanup rc=$rc state=$(cat /sys/kernel/sched_ext/state 2>/dev/null) pins=$(ls -1 "$PIN_ROOT" 2>/dev/null | wc -l)"
}
trap cleanup EXIT

python3 - "$BASELINE" <<'BASELINE'
import json, sys
json.dump({
  "apiVersion": "eulerpilot.io/v1alpha1", "kind": "SchedulingPolicy",
  "metadata": {"name": "stall-watch"},
  "spec": {
    "scope": "scheduler-instance", "mode": "class", "expert": "latency-balanced",
    "source": "human",
    "targets": [{"kind": "cgroup", "path": "/sys/fs/cgroup/system.slice",
                 "role": "FOREGROUND", "role_source": "explicit"}],
    "parameters": {"lat_slice_ns": 1000000, "comp_slice_ns": 4000000,
                   "cache_slice_ns": 4000000, "bg_slice_ns": 2000000,
                   "preempt_threshold_ns": 500000, "bg_vtime_pct": 200,
                   "llc_affinity": False},
    "safety": {"min_dwell_ms": 5000, "max_observation_age_ms": 1000,
               "heartbeat_period_ms": 250, "heartbeat_timeout_ms": 2000,
               "policy_ttl_ms": 900000, "quiet_max_regression_pct": 5,
               "evaluation_min_bg_progress_ratio": 0.5, "bg_work_unit": "probe5s",
               "bg_progress_window_ms": 5000, "min_bg_work_units_per_sec": 1,
               "max_bg_wait_ms": 10000, "max_foreground_p99_ms": 60000,
               "fallback": "verified-safe-profile", "allow_affinity_change": False}
  }
}, open(sys.argv[1], "w"))
BASELINE

"$EXECUTORD" --state-dir "$RUN/state" --socket "$SOCK" --skill sched \
  --loader "$LOADER" --sched-ctl "$CTL" >"$RUN/daemon.log" 2>&1 &
DAEMON=$!
for _ in $(seq 1 100); do
  "$EP" status --socket "$SOCK" >/dev/null 2>&1 && break
  sleep 0.1
done
"$EP" apply --file "$BASELINE" --idempotency-key "stall-apply-0001" --ttl-ms 900000 \
  --socket "$SOCK" >"$OUT/apply.json" 2>&1
applied=$(python3 -c "import json;print(json.load(open('$OUT/apply.json')).get('status'))")
[ "$applied" = "VERIFIED" ] || { echo "FATAL apply returned $applied"; exit 1; }
[ "$(cat /sys/kernel/sched_ext/state)" = "enabled" ] || { echo "FATAL not enabled after apply"; exit 1; }
echo "attached: $(cat /sys/kernel/sched_ext/root/ops)"

redis-server --port "$PORT" --daemonize no --save '' --appendonly no --io-threads 1 \
  --maxmemory 512mb --logfile "$OUT/redis.log" >"$OUT/redis.stdout" 2>&1 &
REDIS_PID=$!
for _ in $(seq 1 100); do redis-cli -p "$PORT" ping >/dev/null 2>&1 && break; sleep 0.2; done
if [ "$LOAD_REDIS" = "1" ]; then
python3 - "$KEYPOOL" > "$OUT/populate.redis" <<'GEN'
import sys
count = int(sys.argv[1])
out = sys.stdout.buffer
for i in range(count):
    key = ("k%d" % (i % count)).encode()
    out.write(b"*3\r\n$3\r\nSET\r\n$%d\r\n%s\r\n$64\r\n%s\r\n" % (len(key), key, b"a" * 64))
GEN
redis-cli -p "$PORT" --pipe < "$OUT/populate.redis" > "$OUT/populate.out" 2>&1
echo "dbsize=$(redis-cli -p "$PORT" dbsize)"
fi

# the state poller: one line per second, so "when did it die" is answerable
(
  while :; do
    printf '%s,%s\n' "$(date +%s%3N)" "$(cat /sys/kernel/sched_ext/state 2>/dev/null)" \
      >> "$OUT/state.polls.csv"
    sleep 1
  done
) &
WATCH_PID=$!

if [ "$LOAD_BG" = "1" ]; then
( while :; do "$SVC" bg safe-basic "$BG_WINDOW_S"; done >> "$OUT/bg.ndjson" 2>> "$OUT/bg.err" ) &
BG_PID=$!
fi

inst_now=$(python3 -c "import json;print(json.load(open('$RUN/state/state.json'))['instance_id'])" 2>/dev/null)
BPFTOOL="${BPFTOOL:-/root/ep-sched-build/build/sbin/bpftool}"
dump_stats() {  # read-only, while still attached
  local tag=$1
  [ -n "${inst_now:-}" ] && [ -e "$PIN_ROOT/$inst_now/stats" ] || return 0
  "$BPFTOOL" map dump pinned "$PIN_ROOT/$inst_now/stats" > "$OUT/stats-$tag.txt" 2>&1 || true
}

for r in $(seq 1 "$ROUNDS"); do
  echo "--- round $r: redis=$LOAD_REDIS bg=$LOAD_BG rate=${RATE} for ${ROUND_S}s ---"
  if [ "$LOAD_REDIS" = "1" ]; then
    "$PROBE" "$CONNS" "$RATE" "$((ROUND_S / 2))" GET --port "$PORT" --keypool "$KEYPOOL" \
      --slo-ms 5 --timeout-ms 50 --raw "$OUT/raw-get-$r.bin" > "$OUT/get-$r.json" 2>> "$OUT/probe.err"
    dump_stats "mid-$r"
    "$PROBE" "$CONNS" "$RATE" "$((ROUND_S / 2))" SET --port "$PORT" --keypool "$KEYPOOL" \
      --slo-ms 5 --timeout-ms 50 --raw "$OUT/raw-set-$r.bin" > "$OUT/set-$r.json" 2>> "$OUT/probe.err"
  else
    sleep "$ROUND_S"
    dump_stats "mid-$r"
  fi
  state=$(cat /sys/kernel/sched_ext/state)
  degraded=$("$EP" status --socket "$SOCK" | python3 -c "import json,sys;print(json.load(sys.stdin)['degraded'])")
  echo "  round $r: state=$state degraded=$degraded"
  [ "$state" = "enabled" ] || break
done

kill -KILL "$WATCH_PID" 2>/dev/null; wait "$WATCH_PID" 2>/dev/null
kill -KILL "$BG_PID" 2>/dev/null; wait "$BG_PID" 2>/dev/null
"$EP" status --socket "$SOCK" > "$OUT/status-final.json" 2>&1

python3 - "$OUT" "$dmesg_before" <<'REPORT'
import json, subprocess, sys, os
out, before = sys.argv[1], int(sys.argv[2])
polls = [line.strip().split(",") for line in open(os.path.join(out, "state.polls.csv")) if line.strip()]
states = [p[1] for p in polls]
disabled_at = [p[0] for p in polls if p[1] != "enabled"]
print("POLLS samples=%d enabled=%d not_enabled=%d" %
      (len(states), states.count("enabled"), len(states) - states.count("enabled")))
dmesg = subprocess.run(["dmesg"], capture_output=True, text=True).stdout.splitlines()[before:]
stalls = [l for l in dmesg if "runnable task stall" in l or "failed to run for" in l]
errors = [l for l in dmesg if "eulerpilot" in l and "disabled" in l]
print("DMESG_new_stall_lines=%d" % len(stalls))
for line in stalls[:5]:
    print("  STALL %s" % line.strip())
for line in errors[:5]:
    print("  DISABLED %s" % line.strip())
final = json.load(open(os.path.join(out, "status-final.json")))
print("FINAL degraded=%s last_fallback=%r generation=%s" %
      (final.get("degraded"), final.get("last_fallback"), final.get("generation")))
survived = not stalls and not disabled_at and not final.get("degraded")
print("CHECK_SCHEDULER_SURVIVED_SUSTAINED_LOAD=%s" % ("yes" if survived else "no"))
if not stalls and not disabled_at:
    print("CHECK_NO_RUNNABLE_STALL=%s" % "yes")
else:
    print("CHECK_NO_RUNNABLE_STALL=no")
REPORT

# read-only: the new counter must actually be firing, or the fix path is not being taken
BPFTOOL="${BPFTOOL:-/root/ep-sched-build/build/sbin/bpftool}"
inst=$(python3 -c "import json;print(json.load(open('$RUN/state/state.json'))['instance_id'])" 2>/dev/null)
if [ -n "${inst:-}" ] && [ -x "$BPFTOOL" ] && [ -e "$PIN_ROOT/$inst/stats" ]; then
  # Reading a map is not a control write: the executor remains the only writer.
  "$BPFTOOL" map dump pinned "$PIN_ROOT/$inst/stats" > "$OUT/stats-dump.txt" 2>&1 || true
  echo "stats dumped to $OUT/stats-dump.txt ($(grep -c 'value' "$OUT/stats-dump.txt" 2>/dev/null) lines)"
fi
