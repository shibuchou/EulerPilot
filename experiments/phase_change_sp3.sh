#!/usr/bin/env bash
# EulerPilot v2 -- phase-change timeline: does the control plane follow the workload?
#
# Protocol §4 asks for quiet -> noisy-cpu -> noisy-mixed -> quiet on one continuous
# timeline, and for cumulative goodput, SLO-violation area, switch count and recovery.
# The block matrix cannot produce any of that: it measures one steady state per block.
# Here each arm runs the whole timeline once and every phase gets its own probe and its
# own per-request raw file, while the control plane is polled every 2s so a switch can
# be attributed to the phase it happened in.
#
#   phase_change_sp3.sh <ep-executord> <ep> <ep-sched-loader> [out-dir] [repeats] [rate]
#
# Arms: A0 = bare service on the kernel scheduler (no executor at all),
#       D  = executor + autopilot, lease handed over before phase 1.
# Manual; needs root, idle sched_ext, redis-server and the built probes.
set -uo pipefail

EXECUTORD="${1:?usage: phase_change_sp3.sh <ep-executord> <ep> <ep-sched-loader> [out-dir] [repeats] [rate]}"
EP="${2:?missing ep}"
LOADER="${3:?missing ep-sched-loader}"
OUT="${4:-/root/epv2-evidence/phase-change}"
REPEATS="${5:-2}"
RATE="${6:-40000}"
HERE="$(cd "$(dirname "$0")" && pwd)"
CTL="$(dirname "$LOADER")/ep-schedctl"
PROBE="$HERE/redis_openloop_probe"
SVC="$HERE/service_load_probe"
OBSERVER="$HERE/service_observer_sp3.sh"
PIN_ROOT=/sys/fs/bpf/eulerpilot-v2
CONNS=8
KEYPOOL=100000
PORT="${PORT:-7807}"
BG_WINDOW_S=5
QUIET_S="${QUIET_S:-60}"
CPU_S="${CPU_S:-60}"
MIX_S="${MIX_S:-60}"
QUIET2_S="${QUIET2_S:-60}"
SPINNERS="${SPINNERS:-8}"
MEM_THREADS="${MEM_THREADS:-3}"
ARMS="${ARMS:-A0 D}"
RUN=/tmp/ep-phase-change
CG_SVC=/sys/fs/cgroup/phase-svc
CG_BG=/sys/fs/cgroup/phase-bg
SOCK="$RUN/executor.sock"
DAEMON=""; REDIS_PID=""; BG_PID=""; OBS_PID=""; POLL_PID=""
LOAD_PIDS=(); MEM_PIDS=()

[ "$(id -u)" = 0 ] || { echo "requires root"; exit 77; }
[ "$(cat /sys/kernel/sched_ext/state 2>/dev/null)" = "disabled" ] || { echo "sched_ext not idle"; exit 1; }
[ -z "$(pgrep -x ep-executord)" ] || { echo "executor already running"; exit 1; }
for need in "$PROBE" "$SVC" "$OBSERVER"; do
  [ -x "$need" ] || { echo "missing $need"; exit 1; }
done

mkdir -p "$OUT" "$CG_SVC" "$CG_BG"

cleanup() {
  local rc=$?
  for pid in "$POLL_PID" "$OBS_PID" "$BG_PID" "${LOAD_PIDS[@]:-}" "${MEM_PIDS[@]:-}"; do
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

policy_json() {  # file
  python3 - "$1" <<'POLICYGEN'
import json, sys
json.dump({
  "apiVersion": "eulerpilot.io/v1alpha1", "kind": "SchedulingPolicy",
  "metadata": {"name": "phase-d"},
  "spec": {
    "scope": "scheduler-instance", "mode": "class", "expert": "safe-basic", "source": "human",
    "targets": [
      {"kind": "cgroup", "path": "/sys/fs/cgroup/phase-svc", "role": "FOREGROUND",
       "role_source": "explicit"},
      {"kind": "cgroup", "path": "/sys/fs/cgroup/phase-bg", "role": "BACKGROUND",
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

add_cpu_load() {
  for _ in $(seq 1 "$SPINNERS"); do
    (while :; do :; done) </dev/null >/dev/null 2>&1 9>&- &
    LOAD_PIDS+=("$!")
  done
}
drop_cpu_load() {
  for pid in "${LOAD_PIDS[@]:-}"; do kill -KILL "$pid" 2>/dev/null; wait "$pid" 2>/dev/null; done
  LOAD_PIDS=()
}
add_mem_load() {
  for _ in $(seq 1 "$MEM_THREADS"); do
    python3 -c '
import array, time
buf = array.array("b", bytes(96 << 20))
end = time.time() + 900
while time.time() < end:
    for i in range(0, len(buf), 512):
        buf[i] = (buf[i] + 1) & 0x7f
' >/dev/null 2>&1 9>&- &
    MEM_PIDS+=("$!")
  done
}
drop_mem_load() {
  for pid in "${MEM_PIDS[@]:-}"; do kill -KILL "$pid" 2>/dev/null; wait "$pid" 2>/dev/null; done
  MEM_PIDS=()
}

phase() {  # dir label seconds
  local d=$1 label=$2 seconds=$3
  echo "    phase $label ${seconds}s"
  cp /proc/pressure/cpu "$d/psi-cpu-$label.before.txt" 2>/dev/null || true
  cp /proc/pressure/memory "$d/psi-mem-$label.before.txt" 2>/dev/null || true
  "$PROBE" "$CONNS" "$RATE" "$seconds" GET --port "$PORT" --keypool "$KEYPOOL" \
    --slo-ms 5 --timeout-ms 50 --raw "$d/raw-$label.bin" > "$d/$label.json" 2>>"$d/probe.err"
  cp /proc/pressure/cpu "$d/psi-cpu-$label.after.txt" 2>/dev/null || true
  cp /proc/pressure/memory "$d/psi-mem-$label.after.txt" 2>/dev/null || true
  cat /proc/loadavg > "$d/loadavg-$label.txt" 2>/dev/null || true
}

run_arm() {  # repeat arm
  local rep=$1 arm=$2 d="$OUT/r$1-$2"
  mkdir -p "$d"; rm -rf "$RUN/state"; mkdir -p "$RUN/state"
  local fair=0
  [ "$arm" = "A0" ] && fair=1

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

  if [ "$fair" = "1" ]; then
    echo "  [$rep/A0] bare service on the kernel scheduler (no executor)"
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
    "$EP" apply --file "$d/policy.json" --idempotency-key "phase-$rep-$arm" --ttl-ms 900000 \
      --socket "$SOCK" >"$d/apply.json" 2>&1
    local status
    status=$(python3 -c "import json;print(json.load(open('$d/apply.json')).get('status'))")
    if [ "$status" != "VERIFIED" ]; then
      echo "$arm: apply returned $status" >> "$d/block.invalid"; stop_all; return 1
    fi
    (
      bash "$OBSERVER" "$EP" "$SOCK" "$SVC" "$d/observer" 5 1 900 >/dev/null 2>&1
    ) 9>&- &
    OBS_PID=$!
  fi

  (
    echo "$BASHPID" > "$CG_BG/cgroup.procs"
    while :; do "$SVC" bg safe-basic "$BG_WINDOW_S"; done
  ) >>"$d/bg.ndjson" 2>>"$d/bg.err" 9>&- &
  BG_PID=$!

  if [ "$fair" = "0" ]; then
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
  fi

  phase "$d" quiet1 "$QUIET_S"
  add_cpu_load;              phase "$d" cpu   "$CPU_S";   drop_cpu_load
  add_cpu_load; add_mem_load; phase "$d" mix  "$MIX_S";   drop_mem_load; drop_cpu_load
  phase "$d" quiet2 "$QUIET2_S"

  [ -n "$DAEMON" ] && "$EP" status --socket "$SOCK" > "$d/status-final.json" 2>&1
  kill -KILL "$POLL_PID" 2>/dev/null; wait "$POLL_PID" 2>/dev/null; POLL_PID=""
  stop_all
  (cd "$d" && sha256sum ./* 2>/dev/null | grep -v SHA256SUMS > SHA256SUMS)
  echo "  [$rep/$arm] done"
}

for rep in $(seq 1 "$REPEATS"); do
  for arm in $ARMS; do
    run_arm "$rep" "$arm" || echo "repeat $rep arm $arm failed"
  done
done

python3 - "$OUT" "$QUIET_S" "$CPU_S" "$MIX_S" "$QUIET2_S" <<'REPORT'
import glob, json, os, struct, sys

root = sys.argv[1]
durations = {"quiet1": int(sys.argv[2]), "cpu": int(sys.argv[3]),
             "mix": int(sys.argv[4]), "quiet2": int(sys.argv[5])}
PHASES = [("quiet1", "quiet"), ("cpu", "noisy-cpu"), ("mix", "noisy-mixed"), ("quiet2", "quiet-again")]


def pct(vals, q):
    idx = int(-(-q * len(vals) // 100)) - 1
    return vals[min(max(idx, 0), len(vals) - 1)]


def load(d, label):
    raw = os.path.join(d, "raw-%s.bin" % label)
    doc_path = os.path.join(d, "%s.json" % label)
    if not (os.path.exists(raw) and os.path.exists(doc_path)):
        return None
    doc = json.load(open(doc_path))
    blob = open(raw, "rb").read()
    vals = sorted(struct.unpack("<%dQ" % (len(blob) // 8), blob))
    if not vals:
        return None
    elapsed = (doc["finished_at_ms"] - doc["offered_at_ms"]) / 1000.0
    return {"goodput": sum(1 for v in vals if v <= 5e6) / elapsed,
            "p50": pct(vals, 50) / 1e6, "p99": pct(vals, 99) / 1e6,
            "elapsed": elapsed, "offered": doc.get("offered") or 0,
            "achieved": doc.get("achieved_rps") or 0.0,
            "inflight": doc.get("peak_inflight")}


rows = []
for d in sorted(glob.glob(os.path.join(root, "r*-*"))):
    if not os.path.isdir(d) or os.path.exists(os.path.join(d, "block.invalid")):
        continue
    rep, arm = os.path.basename(d)[1:].split("-", 1)
    rec = {"rep": int(rep), "arm": arm, "phases": {}}
    for label, _ in PHASES:
        rec["phases"][label] = load(d, label)
    polls = []
    sp = os.path.join(d, "status.csv")
    if os.path.exists(sp):
        for line in open(sp):
            if "," in line:
                ts, rest = line.strip().split(",", 1)
                fields = rest.split(";")
                if len(fields) >= 6:
                    polls.append((int(ts), fields))
    rec["polls"] = polls
    rows.append(rec)

print("runs: %d" % len(rows))
for rec in rows:
    line = "r%-2d %-3s" % (rec["rep"], rec["arm"])
    for label, _ in PHASES:
        v = rec["phases"][label]
        line += " | %-11s gp %7s p99 %6s" % (
            label, ("%.0f" % v["goodput"]) if v else "-", ("%.2f" % v["p99"]) if v else "-")
    print(line)

# Where did the pilot switch? Attribute each counter change to a phase by elapsed time.
for rec in rows:
    if not rec["polls"]:
        continue
    t0 = rec["polls"][0][0]
    phase_ends = {}
    acc = 0
    for label, _ in PHASES:
        acc += durations[label]
        phase_ends[label] = t0 + acc * 1000
    switches = []
    prev = None
    for ts, f in rec["polls"]:
        try:
            n = int(f[1])
        except ValueError:
            continue
        if prev is not None and n != prev:
            label = next((l for l in [p for p, _ in PHASES] if ts <= phase_ends[l]), "after")
            switches.append((label, f[0]))
        prev = n
    print("r%-2d %-3s switches=%d %s holds=%s" % (
        rec["rep"], rec["arm"], len(switches), switches,
        sorted({f[2][:40] for _, f in rec["polls"]})))

print()
for arm in sorted({r["arm"] for r in rows}):
    subset = [r for r in rows if r["arm"] == arm]
    phases = [v for r in subset for v in (r["phases"][l] for l, _ in PHASES) if v]
    total_s = sum(v["elapsed"] for v in phases)
    good_n = sum(v["goodput"] * v["elapsed"] for v in phases)
    done_n = sum(v["achieved"] * v["elapsed"] for v in phases)
    offered_n = sum(v["offered"] for v in phases)
    print("%-3s timelines=%d measured %.0fs cumulative goodput@5ms %.0f/s (delivered %.0f/s), "
          "over-budget %.2f%%, unfinished %.2f%%" % (
              arm, len(subset), total_s, good_n / total_s if total_s else 0,
              done_n / total_s if total_s else 0,
              100.0 * (done_n - good_n) / done_n if done_n else 0,
              100.0 * (offered_n - done_n) / offered_n if offered_n else 0))
    # recovery: compare last quiet phase against the first one
    q1 = [r["phases"]["quiet1"] for r in subset if r["phases"]["quiet1"]]
    q2 = [r["phases"]["quiet2"] for r in subset if r["phases"]["quiet2"]]
    if q1 and q2:
        g1 = sum(v["goodput"] for v in q1) / len(q1)
        g2 = sum(v["goodput"] for v in q2) / len(q2)
        p1 = sum(v["p99"] for v in q1) / len(q1)
        p2 = sum(v["p99"] for v in q2) / len(q2)
        print("    recovery: quiet-again vs quiet goodput %+.2f%%, p99 %+.2f%%" % (
            100.0 * (g2 / g1 - 1.0) if g1 else 0, 100.0 * (p2 / p1 - 1.0) if p1 else 0))
REPORT
