#!/usr/bin/env bash
# EulerPilot v2 -- does the scheduler still survive once LAT/CACHE are actually fed?
#
# The flagship matrix measured a data plane whose class_map had exactly one writer
# path (BACKGROUND), so SP_DSQ_LAT and SP_DSQ_CACHE_* were never fed and the
# runnable-stall investigation never touched those routes. Policy targets can now
# declare a class, so this soak puts real traffic on all three tiers and asks only
# the survival question: does the kernel keep the scheduler registered under
# sustained service load when tasks are routed into LAT, CACHE and BG?
#
# Every process is placed in a cgroup this run owns, and the *policy* declares those
# cgroups -- the executor stays the only writer of class_map and cfg.
#
#   class_route_soak_sp3.sh <ep-executord> <ep> <ep-sched-loader> [out-dir] [rounds] [seconds]
#
# Needs root, an idle sched_ext, redis-server, and the built probes next to this script.
set -uo pipefail

EXECUTORD="${1:?usage: class_route_soak_sp3.sh <ep-executord> <ep> <ep-sched-loader> [out-dir] [rounds] [seconds]}"
EP="${2:?missing ep}"
LOADER="${3:?missing ep-sched-loader}"
OUT="${4:-/root/epv2-evidence/class-route-soak}"
ROUNDS="${5:-3}"
ROUND_S="${6:-90}"
HERE="$(cd "$(dirname "$0")" && pwd)"
CTL="$(dirname "$LOADER")/ep-schedctl"
PROBE="$HERE/redis_openloop_probe"
SVC="$HERE/service_load_probe"
PIN_ROOT=/sys/fs/bpf/eulerpilot-v2
RATE="${RATE:-60000}"
CONNS="${CONNS:-8}"
KEYPOOL="${KEYPOOL:-100000}"
PORT="${PORT:-7801}"
BG_WINDOW_S="${BG_WINDOW_S:-5}"
CACHE_THREADS="${CACHE_THREADS:-2}"
CG_FG=/sys/fs/cgroup/ep-soak-fg
CG_CACHE=/sys/fs/cgroup/ep-soak-cache
CG_BG=/sys/fs/cgroup/ep-soak-bg
RUN=/tmp/ep-class-soak

[ "$(id -u)" = 0 ] || { echo "requires root"; exit 77; }
[ "$(cat /sys/kernel/sched_ext/state 2>/dev/null)" = "disabled" ] || { echo "sched_ext not idle"; exit 1; }
[ -z "$(pgrep -x ep-executord)" ] || { echo "executor already running"; exit 1; }
for need in "$PROBE" "$SVC"; do
  [ -x "$need" ] || { echo "missing $need (compile experiments/*.c)"; exit 1; }
done

mkdir -p "$OUT"
rm -rf "$RUN"; mkdir -p "$RUN/state"
SOCK="$RUN/executor.sock"; BASELINE="$RUN/baseline.json"
DAEMON=""; REDIS_PID=""; BG_PID=""; WATCH_PID=""; CACHE_PIDS=()
: > "$OUT/state.polls.csv"
dmesg_before=$(dmesg | wc -l)

cleanup() {
  local rc=$?
  for pid in "$WATCH_PID" "$BG_PID" "$REDIS_PID" "${CACHE_PIDS[@]:-}"; do
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
  rmdir "$CG_FG" "$CG_CACHE" "$CG_BG" 2>/dev/null
  echo "cleanup rc=$rc state=$(cat /sys/kernel/sched_ext/state 2>/dev/null) pins=$(ls -1 "$PIN_ROOT" 2>/dev/null | wc -l)"
}
trap cleanup EXIT

python3 - "$BASELINE" <<'BASELINE'
import json, sys
json.dump({
  "apiVersion": "eulerpilot.io/v1alpha1", "kind": "SchedulingPolicy",
  "metadata": {"name": "class-route-soak"},
  "spec": {
    "scope": "scheduler-instance", "mode": "class", "expert": "latency-balanced",
    "source": "human",
    # Every tier a policy may now declare: LAT for the served process, CACHE for the
    # memory-bound spinners, BG through the business role (class defaults to BG there).
    "targets": [
      {"kind": "cgroup", "path": "/sys/fs/cgroup/ep-soak-fg",
       "role": "FOREGROUND", "role_source": "explicit", "class": "LAT"},
      {"kind": "cgroup", "path": "/sys/fs/cgroup/ep-soak-cache",
       "role": "NEUTRAL", "role_source": "explicit", "class": "CACHE"},
      {"kind": "cgroup", "path": "/sys/fs/cgroup/ep-soak-bg",
       "role": "BACKGROUND", "role_source": "explicit"}
    ],
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

mkdir -p "$CG_FG" "$CG_CACHE" "$CG_BG"

"$EXECUTORD" --state-dir "$RUN/state" --socket "$SOCK" --skill sched \
  --loader "$LOADER" --sched-ctl "$CTL" >"$RUN/daemon.log" 2>&1 &
DAEMON=$!
for _ in $(seq 1 100); do
  "$EP" status --socket "$SOCK" >/dev/null 2>&1 && break
  sleep 0.1
done
"$EP" apply --file "$BASELINE" --idempotency-key "class-soak-0001" --ttl-ms 900000 \
  --socket "$SOCK" >"$OUT/apply.json" 2>&1
applied=$(python3 -c "import json;print(json.load(open('$OUT/apply.json')).get('status'))")
[ "$applied" = "VERIFIED" ] || { echo "FATAL apply returned $applied"; tail -3 "$OUT/apply.json"; exit 1; }
[ "$(cat /sys/kernel/sched_ext/state)" = "enabled" ] || { echo "FATAL not enabled"; exit 1; }
python3 - "$OUT/apply.json" <<'BINDINGS'
import json, sys
reply = json.load(open(sys.argv[1]))
declared = sorted({(b.get("class"), b.get("effective_class")) for b in reply["target_bindings"]})
print("DECLARED_EFFECTIVE_CLASSES %s" % declared)
BINDINGS

redis-server --port "$PORT" --daemonize no --save '' --appendonly no --io-threads 1 \
  --maxmemory 512mb --logfile "$OUT/redis.log" >"$OUT/redis.stdout" 2>&1 &
REDIS_PID=$!
echo "$REDIS_PID" > "$CG_FG/cgroup.procs"
for _ in $(seq 1 100); do redis-cli -p "$PORT" ping >/dev/null 2>&1 && break; sleep 0.2; done
python3 - "$KEYPOOL" > "$RUN/populate.redis" <<'GEN'
import sys
count = int(sys.argv[1])
out = sys.stdout.buffer
for i in range(count):
    key = ("k%d" % (i % count)).encode()
    out.write(b"*3\r\n$3\r\nSET\r\n$%d\r\n%s\r\n$64\r\n%s\r\n" % (len(key), key, b"a" * 64))
GEN
redis-cli -p "$PORT" --pipe < "$RUN/populate.redis" > "$OUT/populate.out" 2>&1
echo "dbsize=$(redis-cli -p "$PORT" dbsize)"

# CACHE tier: memory-bound spinners that touch a large working set in a loop.
for i in $(seq 1 "$CACHE_THREADS"); do
  (
    # $$ is the *invoking* shell in a subshell; only BASHPID names the process that
    # will actually run the loop, and moving the wrong pid leaves the tier unfed.
    echo "$BASHPID" > "$CG_CACHE/cgroup.procs"
    python3 -c '
import array, time
buf = array.array("b", bytes(64 << 20))
end = time.time() + 600
while time.time() < end:
    for i in range(0, len(buf), 4096):
        buf[i] = 1
' > /dev/null 2>&1
  ) &
  CACHE_PIDS+=("$!")
done

(
  while :; do
    printf '%s,%s\n' "$(date +%s%3N)" "$(cat /sys/kernel/sched_ext/state 2>/dev/null)" >> "$OUT/state.polls.csv"
    sleep 1
  done
) &
WATCH_PID=$!

# BG tier: the same producer the online gate reads, inside the declared cgroup.
(
  echo "$BASHPID" > "$CG_BG/cgroup.procs"
  while :; do "$SVC" bg safe-basic "$BG_WINDOW_S"; done
) >> "$OUT/bg.ndjson" 2>> "$OUT/bg.err" &
BG_PID=$!
sleep 2

inst=$(python3 -c "import json;print(json.load(open('$RUN/state/state.json'))['instance_id'])" 2>/dev/null)
BPFTOOL="${BPFTOOL:-/root/ep-sched-build/build/sbin/bpftool}"

for r in $(seq 1 "$ROUNDS"); do
  echo "--- round $r: ${RATE} rps for ${ROUND_S}s (LAT=redis  CACHE=$CACHE_THREADS  BG=probe) ---"
  "$PROBE" "$CONNS" "$RATE" "$ROUND_S" GET --port "$PORT" --keypool "$KEYPOOL" \
    --slo-ms 5 --timeout-ms 50 --raw "$OUT/raw-get-$r.bin" > "$OUT/get-$r.json" 2>> "$OUT/probe.err"
  [ -e "$PIN_ROOT/$inst/stats" ] && "$BPFTOOL" map dump pinned "$PIN_ROOT/$inst/stats" \
    > "$OUT/stats-mid-$r.txt" 2>&1 || true
  "$PROBE" "$CONNS" "$RATE" "$ROUND_S" SET --port "$PORT" --keypool "$KEYPOOL" \
    --slo-ms 5 --timeout-ms 50 --raw "$OUT/raw-set-$r.bin" > "$OUT/set-$r.json" 2>> "$OUT/probe.err"
  state=$(cat /sys/kernel/sched_ext/state)
  degraded=$("$EP" status --socket "$SOCK" | python3 -c "import json,sys;print(json.load(sys.stdin)['degraded'])")
  echo "  round $r: state=$state degraded=$degraded"
  [ "$state" = "enabled" ] || break
done

kill -KILL "$WATCH_PID" 2>/dev/null; wait "$WATCH_PID" 2>/dev/null
kill -KILL "$BG_PID" 2>/dev/null; wait "$BG_PID" 2>/dev/null
for pid in "${CACHE_PIDS[@]:-}"; do kill -KILL "$pid" 2>/dev/null; wait "$pid" 2>/dev/null; done
"$EP" status --socket "$SOCK" > "$OUT/status-final.json" 2>&1

python3 - "$OUT" "$dmesg_before" <<'REPORT'
import json, os, re, subprocess, sys
out, before = sys.argv[1], int(sys.argv[2])
polls = [l.strip().split(",") for l in open(os.path.join(out, "state.polls.csv")) if l.strip()]
not_enabled = [p[0] for p in polls if p[1] != "enabled"]
dmesg = subprocess.run(["dmesg"], capture_output=True, text=True).stdout.splitlines()[before:]
stalls = [l for l in dmesg if "runnable task stall" in l or "failed to run for" in l]
errors = [l for l in dmesg if "eulerpilot" in l and "disabled" in l]
print("POLLS samples=%d enabled=%d not_enabled=%d" % (len(polls), len(polls) - len(not_enabled), len(not_enabled)))
for line in stalls[:4]:
    print("  STALL %s" % line.strip())
for line in errors[:4]:
    print("  DISABLED %s" % line.strip())
final = json.load(open(os.path.join(out, "status-final.json")))
print("FINAL degraded=%s last_fallback=%r" % (final.get("degraded"), final.get("last_fallback")))

STAT = {0: "enq_lat", 1: "enq_comp", 2: "enq_cache", 3: "enq_bg", 4: "enq_shared",
        5: "dsp_lat", 6: "dsp_comp", 7: "dsp_cache", 8: "dsp_shared",
        10: "run_lat", 11: "run_comp", 12: "run_cache", 13: "run_bg",
        20: "classmap_hit", 21: "classmap_miss", 33: "classmap_expired",
        37: "enq_kick", 38: "enq_pinned_local"}


def decode(path):
    totals, cur, pending = {}, None, False
    for line in open(path).read().splitlines():
        s = line.strip()
        if s == "key:":
            pending = True
            continue
        if pending and re.fullmatch(r"[0-9a-f ]+", s):
            cur = int.from_bytes(bytes.fromhex(s.replace(" ", "")), "little")
            totals.setdefault(cur, 0)
            pending = False
            continue
        m = re.match(r"value \(CPU \d+\): ([0-9a-f ]+)", s)
        if m and cur is not None:
            totals[cur] += int.from_bytes(bytes.fromhex(m.group(1).replace(" ", "")), "little")
    return {STAT.get(k, "idx%d" % k): v for k, v in totals.items() if v}


mid = None
for name in sorted(os.listdir(out)):
    if name.startswith("stats-mid-"):
        mid = decode(os.path.join(out, name))
if mid is None:
    print("STATS no mid-run dump found")
else:
    print("STATS %s" % json.dumps(mid, sort_keys=True))
    for key in ("enq_lat", "enq_cache", "enq_bg", "dsp_lat", "dsp_cache"):
        print("CHECK_FEEDS_%s=%s" % (key.upper(), "yes" if mid.get(key) else "no"))
survived = not stalls and not not_enabled and not final.get("degraded")
print("CHECK_SURVIVES_WITH_CLASS_ROUTING=%s" % ("yes" if survived else "no"))
REPORT
