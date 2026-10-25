#!/usr/bin/env bash
# EulerPilot v2 -- pilot blocks for the experiment matrix (protocol 6.1).
#
# Purpose is explicitly NOT a performance result: a pilot exists to find out
# whether the tooling measures what the protocol claims it measures, how much
# variance this machine actually has, and where the harness lies to us. The
# flagship needs 20 blocks per arm; n is chosen from what this reports.
#
# Arms (protocol 3): A0 default fair + measurement only, C CLASS with a fixed
# expert, D ADAPTIVE with routing and gate active. Redis GET and SET are run and
# reported separately (protocol 4); a background worker exports real completed
# work units so BG progress is measurable rather than assumed.
#
#   pilot_matrix_sp3.sh <ep-executord> <ep> <ep-sched-loader> [out-dir]
#
# Needs root, an idle sched_ext and redis-server/redis-benchmark. Manual.
set -uo pipefail

EXECUTORD="${1:?usage: pilot_matrix_sp3.sh <ep-executord> <ep> <ep-sched-loader> [out-dir]}"
EP="${2:?missing ep}"
LOADER="${3:?missing ep-sched-loader}"
OUT="${4:-/root/epv2-evidence/pilot-matrix}"
CTL="$(dirname "$LOADER")/ep-schedctl"
PIN_ROOT=/sys/fs/bpf/eulerpilot-v2
BLOCKS="${BLOCKS:-3}"
WARMUP_S="${WARMUP_S:-10}"
N_REQ="${N_REQ:-200000}"
CONNS="${CONNS:-50}"
PIPE="${PIPE:-16}"
DATASIZE="${DATASIZE:-64}"
KEYPOOL="${KEYPOOL:-100000}"
SEED="${SEED:-20261023}"
PORT="${PORT:-7777}"
WASHOUT_S="${WASHOUT_S:-15}"
RUN=/tmp/ep-pilot

[ "$(id -u)" = 0 ] || { echo "requires root"; exit 77; }
command -v redis-server >/dev/null || { echo "redis-server missing"; exit 77; }
[ "$(cat /sys/kernel/sched_ext/state 2>/dev/null)" = "disabled" ] || { echo "sched_ext not idle"; exit 1; }
[ -z "$(pgrep -x ep-executord)" ] || { echo "executor already running"; exit 1; }

mkdir -p "$OUT"
rm -rf "$RUN"; mkdir -p "$RUN"
REDIS_PID=""
DAEMON=""
BG_PID=""

stop_all() {
  [ -n "$BG_PID" ] && kill -KILL "$BG_PID" 2>/dev/null; wait "$BG_PID" 2>/dev/null; BG_PID=""
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
cleanup() {
  local rc=$?
  stop_all
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

# ---------------------------------------------------------------- frozen inputs
cat >"$RUN/policy-class.json" <<'JSON'
{
  "apiVersion": "eulerpilot.io/v1alpha1", "kind": "SchedulingPolicy",
  "metadata": {"name": "pilot-class"},
  "spec": {
    "scope": "scheduler-instance", "mode": "class", "expert": "latency-balanced",
    "source": "human",
    "targets": [
      {"kind": "cgroup", "path": "/sys/fs/cgroup/system.slice", "role": "FOREGROUND", "role_source": "explicit"}
    ],
    "parameters": {"lat_slice_ns": 1000000, "comp_slice_ns": 4000000,
                   "cache_slice_ns": 4000000, "bg_slice_ns": 2000000,
                   "preempt_threshold_ns": 500000, "bg_vtime_pct": 200, "llc_affinity": false},
    "safety": {"min_dwell_ms": 5000, "max_observation_age_ms": 1000,
               "heartbeat_period_ms": 250, "heartbeat_timeout_ms": 2000,
               "policy_ttl_ms": 900000, "quiet_max_regression_pct": 5,
               "evaluation_min_bg_progress_ratio": 0.5, "bg_work_unit": "chunks",
               "bg_progress_window_ms": 5000, "min_bg_work_units_per_sec": 1,
               "max_bg_wait_ms": 5000, "max_foreground_p99_ms": 50,
               "fallback": "verified-safe-profile", "allow_affinity_change": false}
  }
}
JSON
cp "$RUN/policy-class.json" "$RUN/policy-adaptive.json"
python3 - "$RUN/policy-adaptive.json" <<'PY'
import json, sys
path = sys.argv[1]
policy = json.load(open(path))
policy["metadata"]["name"] = "pilot-adaptive"
json.dump(policy, open(path, "w"))
PY

python3 - "$RUN/bgpilot.py" <<'PY'
import sys
open(sys.argv[1], "w").write('''
import hashlib, os, sys, time
# One work unit = a fixed amount of real CPU work. Progress is exported so the
# protocol's "pressure generator must report true progress" holds.
unit = os.environ.get("BG_UNIT_FILE", "/tmp/ep-pilot/bg.units")
stop = os.environ.get("BG_STOP_FILE", "/tmp/ep-pilot/bg.stop")
buf = bytearray(b"x" * 65536)
n = 0
with open(unit, "w") as fh:
    fh.write("0\\n"); fh.flush()
while not os.path.exists(stop):
    for _ in range(200):
        hashlib.sha256(bytes(buf)).digest()
    n += 1
    with open(unit, "w") as fh:
        fh.write("%d\\n" % n)
    time.sleep(0.05)
''')
PY

manifest_head() {  # block arm
  python3 - "$OUT/$1-$2/env.json" "$1" "$2" "$EXECUTORD" "$EP" "$LOADER" "$SEED" "$PORT" \
    "$N_REQ" "$CONNS" "$PIPE" "$DATASIZE" "$KEYPOOL" "$WARMUP_S" <<'PY'
import hashlib, json, os, platform, subprocess, sys
(path, block, arm, executord, ep, loader, seed, port, nreq, conns, pipe, datasize,
 keypool, warmup) = sys.argv[1:15]
def sha(f):
    return hashlib.sha256(open(f, "rb").read()).hexdigest() if os.path.exists(f) else None
def read(p):
    try:
        return open(p).read().strip()
    except OSError:
        return None
loadavg = list(os.getloadavg())
json.dump({
  "block": int(block), "arm": arm, "seed": int(seed),
  "started_at": subprocess.check_output(["date", "-Is"]).decode().strip(),
  "kernel": platform.release(), "hostname": platform.node(), "cpus": os.cpu_count(),
  "loadavg_before": loadavg,
  "binaries": {"ep-executord": sha(executord), "ep": sha(ep), "ep-sched-loader": sha(loader)},
  "sched_ext": {"state": read("/sys/kernel/sched_ext/state"), "ops": read("/sys/kernel/sched_ext/root/ops")},
  "redis": {"port": int(port), "version": subprocess.check_output(["redis-server", "--version"]).decode().split()[-1],
            "clients": int(conns), "pipeline": int(pipe), "requests": int(nreq),
            "datasize": int(datasize), "keypool": int(keypool),
            "config": "appendonly no; save \"\"; io-threads 1; maxmemory 512mb; --dbsize default"},
  "warmup_s": int(warmup),
}, open(path, "w"), indent=2, sort_keys=True)
PY
}

# ------------------------------------------------------------------------ arms
start_redis() {
  redis-server --port "$PORT" --daemonize no --save '' --appendonly no --io-threads 1 \
    --maxmemory 512mb --logfile "$D/redis.log" --databases 16 >"$D/redis.stdout" 2>&1 &
  REDIS_PID=$!
  for _ in $(seq 1 100); do redis-cli -p "$PORT" ping >/dev/null 2>&1 && break; sleep 0.2; done
  redis-cli -p "$PORT" ping >"$D/redis.ping" 2>&1 || { echo "redis did not come up"; return 1; }
  # Populate the whole keypool: with 20k keys against a 100k keypool most GETs
  # measure a miss path, and the arm comparison silently changes meaning.
  # Identical dataset per block: same keypool, same value size, freshly populated.
  redis-benchmark -p "$PORT" -c "$CONNS" -n "$KEYPOOL" -r "$KEYPOOL" -d "$DATASIZE" -t set -q \
    >"$D/populate.txt" 2>&1
  redis-cli -p "$PORT" dbsize >"$D/redis.dbsize" 2>&1
}

start_arm() {  # arm
  local arm=$1
  case "$arm" in
    A0) return 0 ;;
    C)
      rm -rf "$RUN/state"; mkdir -p "$RUN/state"
      "$EXECUTORD" --state-dir "$RUN/state" --socket "$RUN/executor.sock" --skill sched \
        --loader "$LOADER" --sched-ctl "$CTL" >"$D/daemon.log" 2>&1 &
      DAEMON=$!
      for _ in $(seq 1 100); do "$EP" status --socket "$RUN/executor.sock" >/dev/null 2>&1 && break; sleep 0.1; done
      "$EP" apply --file "$RUN/policy-class.json" --idempotency-key "pilot-c-$BLOCK" \
        --ttl-ms 900000 --socket "$RUN/executor.sock" >"$D/apply.json" 2>&1
      [ "$(python3 -c "import json;print(json.load(open('$D/apply.json'))['status'])" 2>/dev/null)" = "VERIFIED" ] ||
        { echo "arm C apply failed"; cat "$D/apply.json"; return 1; }
      ;;
    D)
      rm -rf "$RUN/state"; mkdir -p "$RUN/state"
      "$EXECUTORD" --state-dir "$RUN/state" --socket "$RUN/executor.sock" --skill sched \
        --loader "$LOADER" --sched-ctl "$CTL" \
        --auto-apply --auto-policy "$RUN/policy-adaptive.json" --auto-owner autopilot-v2 \
        --auto-min-interval-ms 3000 --auto-lease-ttl-ms 900000 --auto-dwell-ms 10000 \
        --auto-candidate-hold-ms 5000 --auto-budget-window-ms 600000 --auto-budget-max-switches 4 \
        >"$D/daemon.log" 2>&1 &
      DAEMON=$!
      for _ in $(seq 1 100); do "$EP" status --socket "$RUN/executor.sock" >/dev/null 2>&1 && break; sleep 0.1; done
      "$EP" apply --file "$RUN/policy-adaptive.json" --idempotency-key "pilot-d-$BLOCK" \
        --ttl-ms 900000 --socket "$RUN/executor.sock" >"$D/apply.json" 2>&1
      "$EP" lease --owner autopilot-v2 --ttl-ms 900000 --socket "$RUN/executor.sock" >"$D/handover.json" 2>&1
      ;;
  esac
  return 0
}

run_block() {  # block arm
  BLOCK=$1; D="$OUT/$1-$2"; mkdir -p "$D"
  export BG_UNIT_FILE="$D/bg.units" BG_STOP_FILE="$D/bg.stop"
  manifest_head "$1" "$2"
  start_redis || return 1
  python3 "$RUN/bgpilot.py" &
  BG_PID=$!
  start_arm "$2" || return 1
  sleep "$WARMUP_S"
  cat "$BG_UNIT_FILE" > "$D/bg.before" 2>/dev/null || echo 0 > "$D/bg.before"
  for t in get set; do
    redis-benchmark -p "$PORT" -c "$CONNS" -n "$N_REQ" -r "$KEYPOOL" -d "$DATASIZE" \
      -P "$PIPE" -t "$t" --csv > "$D/bench-$t.csv" 2> "$D/bench-$t.err"
    echo "exit=$?" >> "$D/bench-$t.status"
  done
  cat "$BG_UNIT_FILE" > "$D/bg.after" 2>/dev/null || echo 0 > "$D/bg.after"
  redis-cli -p "$PORT" info clients > "$D/redis.clients" 2>&1
  redis-cli -p "$PORT" info stats > "$D/redis.stats" 2>&1
  uname -r > "$D/kernel.txt" 2>&1
  cat /sys/kernel/sched_ext/state > "$D/schedext.state" 2>/dev/null || echo missing > "$D/schedext.state"
  cat /sys/kernel/sched_ext/root/ops > "$D/schedext.ops" 2>/dev/null || true
  if [ "$2" != "A0" ]; then
    "$EP" status --socket "$RUN/executor.sock" > "$D/status.json" 2>&1
    [ -f "$RUN/state/journal.log" ] && cp "$RUN/state/journal.log" "$D/journal.log" 2>/dev/null
    "$EP" rollback --of "$(python3 -c "import json;print(json.load(open('$D/apply.json'))['action_id'])" 2>/dev/null)" \
      --transaction "$(python3 -c "import json;print(json.load(open('$D/apply.json'))['transaction_id'])" 2>/dev/null)" \
      --snapshot "$(python3 -c "import json;print(json.load(open('$D/apply.json'))['snapshot_id'])" 2>/dev/null)" \
      --policy-sha256 "$(python3 -c "import json;print(json.load(open('$D/apply.json'))['policy_sha256'])" 2>/dev/null)" \
      --socket "$RUN/executor.sock" > "$D/rollback.json" 2>&1 || true
  fi
  touch "$BG_STOP_FILE"
  stop_all
  if [ "$2" != "A0" ] && [ "$(cat /sys/kernel/sched_ext/state 2>/dev/null)" = "disabled" ] &&
     [ -z "$(pgrep -x ep-executord)" ]; then
    # Orphans from this block only: the box is quiescent, so removing them keeps
    # the run's own bookkeeping honest instead of accumulating pins.
    for dir in "$PIN_ROOT"/inst-*; do
      [ -d "$dir" ] && rm -rf "$dir" && echo "removed $dir" >> "$D/pins-removed.txt"
    done
    rmdir "$PIN_ROOT" 2>/dev/null
  fi
  cat /proc/loadavg > "$D/loadavg_after.txt" 2>/dev/null
  find "$RUN/state" -maxdepth 1 -name 'journal*' -exec cp {} "$D/" \; 2>/dev/null
  (cd "$D" && sha256sum ./* 2>/dev/null | grep -v "SHA256SUMS" > SHA256SUMS)
  sleep "$WASHOUT_S"
}

# Protocol 6.3: arm order is randomised per block with a recorded seed.
orders=$(python3 - "$SEED" "$BLOCKS" <<'PY'
import random, sys
seed, blocks = int(sys.argv[1]), int(sys.argv[2])
rng = random.Random(seed)
for b in range(1, blocks + 1):
    order = ["A0", "C", "D"]
    rng.shuffle(order)
    print(b, " ".join(order))
PY
)
echo "$orders" > "$OUT/arm-order.txt"
echo "frozen seed=$SEED arm order per block:"
cat "$OUT/arm-order.txt"

while read -r block arms; do
  [ -n "${block:-}" ] || continue
  for arm in $arms; do
    echo "### block=$block arm=$arm $(date +%T)"
    run_block "$block" "$arm" || echo "block $block arm $arm FAILED (evidence kept in $OUT/$block-$arm)"
  done
done <<< "$orders"

python3 - "$OUT" <<'ANALYSE'
import csv, json, os, statistics, sys
root = sys.argv[1]
rows = []
for entry in sorted(os.listdir(root)):
    path = os.path.join(root, entry)
    if not os.path.isdir(path) or "-" not in entry:
        continue
    block, arm = entry.split("-", 1)
    try:
        env = json.load(open(os.path.join(path, "env.json")))
    except Exception:
        continue
    per_command = {}
    for command in ("get", "set"):
        bench = os.path.join(path, "bench-%s.csv" % command)
        if not os.path.exists(bench):
            continue
        # redis 7.2 --csv header: test,rps,avg_latency_ms,min,p50,p95,p99,max
        csv_rows = list(csv.reader(open(bench, errors="replace")))
        if not csv_rows:
            continue
        header = [c.strip('"') for c in csv_rows[0]]
        parsed = {}
        for line in csv_rows[1:]:
            record = dict(zip(header, [c.strip('"') for c in line]))
            if record.get("test", "").upper() == command.upper():
                parsed = record
                break
        if not parsed:
            continue
        per_command[command] = {
            "rps": float(parsed["rps"]) if parsed.get("rps") else None,
            "p50_ms": float(parsed["p50_latency_ms"]) if parsed.get("p50_latency_ms") else None,
            "p99_ms": float(parsed["p99_latency_ms"]) if parsed.get("p99_latency_ms") else None,
            "max_ms": float(parsed["max_latency_ms"]) if parsed.get("max_latency_ms") else None,
        }
    try:
        before = int(open(os.path.join(path, "bg.before")).read().strip() or 0)
        after = int(open(os.path.join(path, "bg.after")).read().strip() or 0)
    except Exception:
        before = after = 0
    rows.append({"block": block, "arm": arm, "bg_units": after - before,
                 "schedext": (env.get("sched_ext") or {}).get("state"),
                 "loadavg_before": env.get("loadavg_before"), "commands": per_command})
print("collected blocks:", len(rows))
for command in ("get", "set"):
    print("\n== %s ==" % command.upper())
    for arm in ("A0", "C", "D"):
        vals = [r["commands"][command]["rps"] for r in rows
                if r["arm"] == arm and r["commands"].get(command, {}).get("rps")]
        p99s = [r["commands"][command]["p99_ms"] for r in rows
                if r["arm"] == arm and r["commands"].get(command, {}).get("p99_ms")]
        bg = [r["bg_units"] for r in rows if r["arm"] == arm and r["bg_units"]]
        if not vals:
            print("%s: no parseable rps" % arm)
            continue
        mean = statistics.mean(vals)
        cv = 100 * statistics.pstdev(vals) / mean if mean and len(vals) > 1 else 0
        print("%s n=%d rps median=%.0f mean=%.0f CV=%.1f%% p99 median=%s ms bg units median=%s" % (
            arm, len(vals), statistics.median(vals), mean, cv,
            "%.3f" % statistics.median(p99s) if p99s else "none",
            statistics.median(bg) if bg else "none"))
print("\nNOTE pilot numbers are tooling/variance evidence only (protocol 6.1) and are")
print("not entered into the formal table; goodput@SLO still cannot be computed because")
print("redis-benchmark yields no per-request records (protocol 5 requires them).")
ANALYSE
