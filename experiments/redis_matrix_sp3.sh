#!/usr/bin/env bash
# EulerPilot v2 -- matrix block runner with open-loop load, per-request records and A1/A2.
#
# This replaces what the pilot could not do (see evidence/p2/pilot-matrix-20261023.txt):
#   * arrivals are paced (open loop), so a slow service no longer lowers offered load;
#   * every completion is written to a raw file, so the table can be recomputed and
#     goodput@SLO is counted rather than inferred from quantiles;
#   * the measurement window is a real duration, not a 0.4s burst;
#   * A1 (equal observation, mock data plane, no attach) separates monitoring cost from
#     scheduler cost, and A2 (static cgroup weights) shows how much plain tilting explains;
#   * the background work unit is the same producer the online SLO gate reads, so the
#     offline table and the online floor are not two different currencies.
#
#   redis_matrix_sp3.sh <ep-executord> <ep> <ep-sched-loader> [out-dir]
#
# Needs root, redis-server, an idle sched_ext, and the two built probes. The block count
# here is a smoke default; the flagship needs the frozen 20 blocks with the arm order
# recorded from a seed.
set -uo pipefail

EXECUTORD="${1:?usage: redis_matrix_sp3.sh <ep-executord> <ep> <ep-sched-loader> [out-dir]}"
EP="${2:?missing ep}"
LOADER="${3:?missing ep-sched-loader}"
OUT="${4:-/root/epv2-evidence/matrix}"
HERE="$(cd "$(dirname "$0")" && pwd)"
CTL="$(dirname "$LOADER")/ep-schedctl"
PROBE="$HERE/redis_openloop_probe"
SVC="$HERE/service_load_probe"
PIN_ROOT=/sys/fs/bpf/eulerpilot-v2
BLOCKS="${BLOCKS:-1}"
ARMS="${ARMS:-A0 A1 A2 B C D}"
# Reference-control arm E: the SchedPilot daemon (P0 build) with its own
# classifier, pointed at the same workload cgroups. It is the only arm that
# never runs ep-executord; the flagship-quality comparison is E vs A0.
E_DAEMON="${E_DAEMON:-/root/p0/schedpilot-0aeca0c/build/schedpilotd}"
export E_DAEMON
DAEMON_GRACE=0
# Cc / Dc are C and D with exactly one thing added: the policy also declares a runtime
# class for the cgroups the workload actually lives in. Targets, expert, parameters,
# dataset and placement are shared with the plain arms on purpose, so a within-block
# C vs Cc difference is the price of class routing and nothing else.
CLASS_ARMS="${CLASS_ARMS:-Cc Dc}"
# Ablations (protocol §3) change exactly one declared thing relative to their base arm:
#   Dng -- the service half of the SLO gate is made non-binding (floor 0.001 units/s,
#          wait 10000ms, forward ceiling 60000ms; the schema forbids a floor of 0, so
#          "no gate" has to be expressed as "cannot bind" and is reported that way)
#   Dnv -- bg_vtime_pct 100 instead of 200, i.e. no background vtime penalty
# Everything else -- targets, class declarations, expert, dataset, window -- is shared.
ABLATION_ARMS="${ABLATION_ARMS:-Dng Dnv}"
WARMUP_S="${WARMUP_S:-10}"
MEASURE_S="${MEASURE_S:-60}"
RATE="${RATE:-8000}"
CONNS="${CONNS:-8}"
KEYPOOL="${KEYPOOL:-100000}"
SLO_MS="${SLO_MS:-5}"
TIMEOUT_MS="${TIMEOUT_MS:-50}"
PORT="${PORT:-7797}"
BG_WINDOW_S="${BG_WINDOW_S:-5}"
# GET against an empty keyspace replies NIL, which is the cheapest reply redis can
# give, so a measured "GET" block that never populated is a measurement of nothing.
# Protocol 6.4 requires the same initial dataset in every arm; fill it explicitly.
POPULATE_REQUESTS="${POPULATE_REQUESTS:-$KEYPOOL}"
# Online-gate thresholds the matrix declares up front. They are frozen by the pilot
# (evidence/p2/flagship-params-*.txt) and recorded in every block manifest.
BG_FLOOR_UNITS="${BG_FLOOR_UNITS:-20}"
BG_MAX_WAIT_MS="${BG_MAX_WAIT_MS:-5000}"
FG_P99_CEILING_MS="${FG_P99_CEILING_MS:-50}"
SEED="${SEED:-20261023}"
export PORT SLO_MS TIMEOUT_MS BG_FLOOR_UNITS BG_MAX_WAIT_MS FG_P99_CEILING_MS BG_WINDOW_S CG_SVC CG_BG
CG_ROOT=/sys/fs/cgroup/matrix-tilt
# Per-block cgroups holding the service and the background producer. The harness only
# puts processes into them; the tier is declared by the policy and written by the
# executor, so the experiment never becomes a second writer of class_map.
CG_SCOPE_ROOT=/sys/fs/cgroup/matrix-scope
CG_SVC=""
CG_BG=""
RUN=/tmp/ep-matrix
BLOCK=0

for f in "$PROBE" "$SVC"; do
  [ -x "$f" ] || { echo "missing $f (compile experiments/*.c)"; exit 1; }
done
[ "$(id -u)" = 0 ] || { echo "requires root"; exit 77; }
[ "$(cat /sys/kernel/sched_ext/state 2>/dev/null)" = "disabled" ] || { echo "sched_ext not idle"; exit 1; }
[ -z "$(pgrep -x ep-executord)" ] || { echo "executor already running"; exit 1; }

# Two runs sharing one evidence tree silently destroy each other's blocks (this has
# happened), so take the same cooperative lock the acceptance script uses.
exec 9>/run/lock/eulerpilot-v2-matrix.lock
flock -n 9 || { echo "another matrix run holds the lock"; exit 1; }

mkdir -p "$OUT"
rm -rf "$RUN"; mkdir -p "$RUN"
REDIS_PID=""; DAEMON=""; BG_PID=""; OBS_PID=""

stop_all() {
  # The bg worker is a shell loop whose child (service_load_probe) outlives the loop by
  # up to one window; killing only the loop left that child holding the flock fd, so
  # the next run was refused as "another matrix run holds the lock". Long-lived
  # children get 9>&- at exec and strays under the loop are killed before reaping it.
  if [ -n "${BG_PID:-}" ]; then
    for child in $(pgrep -P "$BG_PID" 2>/dev/null); do
      kill -KILL "$child" 2>/dev/null; wait "$child" 2>/dev/null
    done
  fi
  if [ -n "${DAEMON:-}" ] && [ "$DAEMON_GRACE" = "1" ]; then
    kill -TERM "$DAEMON" 2>/dev/null
    for _ in $(seq 1 40); do kill -0 "$DAEMON" 2>/dev/null || break; sleep 0.25; done
  fi
  for pid in "${OBS_PID}" "${BG_PID}" "${DAEMON}"; do
    [ -n "$pid" ] && kill -KILL "$pid" 2>/dev/null; wait "$pid" 2>/dev/null
  done
  DAEMON_GRACE=0
  OBS_PID=""; BG_PID=""; DAEMON=""
  if [ -n "$REDIS_PID" ] && kill -0 "$REDIS_PID" 2>/dev/null; then
    redis-cli -p "$PORT" shutdown nosave >/dev/null 2>&1
    kill -TERM "$REDIS_PID" 2>/dev/null; wait "$REDIS_PID" 2>/dev/null
  fi
  REDIS_PID=""
  rm -rf "$CG_ROOT" 2>/dev/null
  # Cgroup removal is asynchronous; wait for the directories to actually go away, or
  # the next block can place a process on a cgroup that is already being reaped.
  if [ -n "$CG_SVC" ]; then
    for _ in $(seq 1 40); do
      { [ ! -d "$CG_SVC" ] && [ ! -d "$CG_BG" ]; } && break
      rmdir "$CG_SVC" "$CG_BG" 2>/dev/null
      sleep 0.25
    done
    rmdir "$CG_SCOPE_ROOT" 2>/dev/null
    CG_SVC=""; CG_BG=""
  fi
  for _ in $(seq 1 60); do
    [ "$(cat /sys/kernel/sched_ext/state 2>/dev/null)" = "disabled" ] && break
    sleep 0.5
  done
  if [ "$(cat /sys/kernel/sched_ext/state 2>/dev/null)" = "disabled" ] &&
     [ -z "$(pgrep -x ep-executord)" ]; then
    for dir in "$PIN_ROOT"/inst-*; do [ -d "$dir" ] && rm -rf "$dir"; done
    rmdir "$PIN_ROOT" 2>/dev/null
  fi
}
cleanup() {
  local rc=$?
  stop_all
  echo "cleanup rc=$rc state=$(cat /sys/kernel/sched_ext/state 2>/dev/null) pins=$(ls -1 "$PIN_ROOT" 2>/dev/null | wc -l)"
}
trap cleanup EXIT

policy_json() {  # file ... svc_cgroup bg_cgroup class_routing ablation [mode]
  python3 - "$1" "$2" "$3" "$4" "$BG_WINDOW_S" "$5" "$6" "$7" "$8" "${9:-class}" <<'POLICYGEN'
import json, os, sys
path, name, expert, source, bg_window, svc_cg, bg_cg, class_routing, ablation, mode = sys.argv[1:11]
# Every threshold the online gate reads is taken from the environment, so the frozen
# flagship numbers are one visible thing and not three literals inside a generator.
slo = float(os.environ.get("SLO_MS", "5"))
bg_floor = float(os.environ.get("BG_FLOOR_UNITS", "20"))
# Three of these are integer-typed in the schema; passing 5000.0 is a validation
# failure, not a rounding detail (it rejected both scheduler arms in the pilot).
bg_wait = int(float(os.environ.get("BG_MAX_WAIT_MS", "5000")))
fg_ceiling = int(float(os.environ.get("FG_P99_CEILING_MS", "50")))
bg_vtime = 200
if ablation == "no-service-gate":
    # "No gate" is not expressible as an absent field (they are required), and the schema
    # forbids a floor of 0, so the gate is made unable to bind instead. The manifest
    # records the ablation name, and the report states the exact values used.
    bg_floor, bg_wait, fg_ceiling = 0.001, 10000, 60000
elif ablation == "no-bg-penalty":
    bg_vtime = 100
policy = {
  "apiVersion": "eulerpilot.io/v1alpha1", "kind": "SchedulingPolicy",
  "metadata": {"name": name},
  "spec": {
    "scope": "scheduler-instance", "mode": mode, "expert": expert, "source": source,
    # Both arms target the same two cgroups; only the class declarations differ, so a
    # difference between C and Cc cannot be blamed on what is being protected.
    "targets": [
      {"kind": "cgroup", "path": svc_cg, "role": "FOREGROUND", "role_source": "explicit"},
      {"kind": "cgroup", "path": bg_cg, "role": "BACKGROUND", "role_source": "explicit"}
    ],
    "parameters": {"lat_slice_ns": 1000000, "comp_slice_ns": 4000000,
                   "cache_slice_ns": 4000000, "bg_slice_ns": 2000000,
                   "preempt_threshold_ns": 500000, "bg_vtime_pct": bg_vtime,
                   "llc_affinity": False},
    "safety": {"min_dwell_ms": 5000, "max_observation_age_ms": 1000,
               "heartbeat_period_ms": 250, "heartbeat_timeout_ms": 2000,
               "policy_ttl_ms": 900000, "quiet_max_regression_pct": 5,
               "evaluation_min_bg_progress_ratio": 0.5,
               "bg_work_unit": "probe%ss" % bg_window,
               "bg_progress_window_ms": int(bg_window) * 1000,
               "min_bg_work_units_per_sec": bg_floor,
               "max_bg_wait_ms": bg_wait, "max_foreground_p99_ms": fg_ceiling,
               "fallback": "verified-safe-profile", "allow_affinity_change": False}
  }
}
if class_routing == "1":
    # Without these two keys the BACKGROUND target still resolves to BG through the role
    # default and the service resolves to nothing -- that is precisely the shape the
    # flagship matrix measured, which is why the plain arm is the right control.
    policy["spec"]["targets"][0]["class"] = "LAT"
    policy["spec"]["targets"][1]["class"] = "BG"
json.dump(policy, open(path, "w"))
POLICYGEN
}

tilt_proof() {  # dir -- read back the weight the workload actually landed in
  local d=$1 cg weight=""
  sleep 1
  local pid
  pid=$(pgrep -n -x redis-server | head -1)
  [ -n "$pid" ] || { echo "redis pid missing" >> "$d/block.invalid"; return 1; }
  cg=$(awk -F:: '$1=="cpu"||$1=="cpu,cpuacct"{print $3}' "/proc/$pid/cgroup" | head -1)
  [ -n "$cg" ] || cg=$(sed -n 's/^0::\///p' "/proc/$pid/cgroup")
  cg=${cg#/}
  for candidate in "/sys/fs/cgroup/$cg/cpu.weight" "/sys/fs/cgroup/system.slice/../$cg/cpu.weight"; do
    if [ -r "$candidate" ]; then weight=$(cat "$candidate"); break; fi
  done
  echo "redis_pid=$pid cgroup=$cg cpu.weight=${weight:-unreadable}" > "$d/tilt.proof"
  if [ "$weight" != "200" ]; then
    echo "arm A2 did not get the requested CPUWeight (got ${weight:-nothing})" >> "$d/block.invalid"
    return 1
  fi
  return 0
}

start_redis() {  # dir
  local d=$1
  # A2 tilts with static CPU weights only: no sched_ext anywhere in that arm. Writing
  # cpu.weight into a hand-made cgroup is refused on this box (the kernel's cpu
  # controller is not delegated to the root's subtree_control), so systemd scopes are
  # used and the resulting weight is read back and proven, not assumed.
  if [ "${TILT:-}" = "1" ]; then
    command -v systemd-run >/dev/null || { echo "systemd-run missing" >> "$d/block.invalid"; return 1; }
    systemd-run --quiet --scope --unit "ep-mx-fg-$BLOCK" -p CPUWeight=200 \
      redis-server --port "$PORT" --daemonize no --save '' --appendonly no --io-threads 1 \
        --maxmemory 512mb --logfile "$d/redis.log" >"$d/redis.stdout" 2>&1 9>&- &
    sleep 0.5
  else
    redis-server --port "$PORT" --daemonize no --save '' --appendonly no --io-threads 1       --maxmemory 512mb --logfile "$d/redis.log" >"$d/redis.stdout" 2>&1 9>&- &
  fi
  REDIS_PID=$!
  if [ "${TILT:-}" = "1" ]; then tilt_proof "$d"; fi
  for _ in $(seq 1 100); do redis-cli -p "$PORT" ping >/dev/null 2>&1 && break; sleep 0.2; done
  redis-cli -p "$PORT" ping >"$d/redis.ping" 2>&1 || return 1
  redis-cli -p "$PORT" config set save '' >/dev/null 2>&1
  return 0
}

populate_keys() {  # dir -- identical dataset in every arm, before any measurement
  local d=$1 rc keys
  # The probe writes 64-byte values and keys k<seq % keypool>; this has to be the same
  # shape or the measured GETs are not the ones the workload claims to make.
  python3 - "$POPULATE_REQUESTS" "$KEYPOOL" > "$d/populate.redis" 2>> "$d/populate.err" <<'GENPY'
import sys
count, keypool = int(sys.argv[1]), int(sys.argv[2])
# Bytes, not text: RESP line endings are CRLF and a text-mode stdout would translate
# "\n" again on some platforms, turning every command into "$64\r\r\n...".
out = sys.stdout.buffer
for i in range(count):
    key = ("k%d" % (i % keypool)).encode()
    out.write(b"*3\r\n$3\r\nSET\r\n$%d\r\n%s\r\n$64\r\n%s\r\n" % (len(key), key, b"a" * 64))
GENPY
  rc=0
  redis-cli -p "$PORT" --pipe < "$d/populate.redis" > "$d/populate.out" 2>> "$d/populate.err" || rc=$?
  keys=$(redis-cli -p "$PORT" dbsize 2>> "$d/populate.err")
  echo "populate_requests=$POPULATE_REQUESTS dbsize=${keys:-unknown} rc=$rc" > "$d/populate.proof"
  if [ "$rc" != "0" ] || [ -z "${keys:-}" ] || [ "$keys" = "0" ]; then
    echo "dataset not established (dbsize=${keys:-none} rc=$rc)" >> "$d/block.invalid"
    return 1
  fi
  return 0
}

assert_classes() {  # dir arm -- a class arm that did not classify is not a class sample
  local d=$1 arm=$2
  python3 - "$d/apply.json" "$arm" "$d/classes.proof" <<'CLASSPROOF'
import json, sys
path, arm, proof = sys.argv[1:4]
try:
    reply = json.load(open(path))
except Exception as exc:
    print("apply.json unreadable: %r" % exc)
    raise SystemExit(1)
bindings = reply.get("target_bindings") or []
# `or ""` matters: an absent key is None, and sorting tuples that mix None with str
# raises before the proof line is ever written -- which reads as "check failed" when
# what happened is "check crashed".
got = sorted({(b.get("class") or "", b.get("effective_class") or "") for b in bindings})
# The plain arm's background target declares nothing and still resolves to BG through
# the role default -- that pair ("", "BG") is exactly what the class arm changes to
# ("BG", "BG"), so the expectation is written from the contract, not from a guess.
want = {"C": [("", ""), ("", "BG")], "D": [("", ""), ("", "BG")],
        "Dng": [("", ""), ("", "BG")], "Dnv": [("", ""), ("", "BG")],
        "Cc": [("BG", "BG"), ("LAT", "LAT")],
        "Dc": [("BG", "BG"), ("LAT", "LAT")]}.get(arm)
ok = want is None or got == want
with open(proof, "w") as out:  # written before the verdict so the artifact always exists
    print("arm=%s declared_effective=%s expected=%s ok=%s" % (arm, got, want,
          "yes" if ok else "no"), file=out)
if not ok:
    print("class declarations did not land as intended (%s != %s)" % (got, want))
    raise SystemExit(1)
CLASSPROOF
  if [ "$?" != "0" ]; then
    echo "arm $arm class routing not established (see classes.proof)" >> "$d/block.invalid"
    stop_all
    return 1
  fi
  return 0
}

require_attach() {  # dir arm -- a block that did not really attach is not a sample
  local d=$1 arm=$2 status
  status=$(python3 -c "
import json
try:
    print(json.load(open('$d/apply.json')).get('status', ''))
except Exception:
    print('unreadable')
" 2>/dev/null)
  if [ "$status" != "VERIFIED" ] || [ "$(cat /sys/kernel/sched_ext/state 2>/dev/null)" != "enabled" ]; then
    echo "arm $arm did not attach (apply=$status state=$(cat /sys/kernel/sched_ext/state 2>/dev/null))" \
      >> "$d/block.invalid"
    stop_all
    return 1
  fi
  return 0
}

run_bg() {  # dir ; same producer the online gate reads, so units are comparable
  local d=$1
  (
    # $BASHPID, not $$: inside a subshell $$ is the invoking shell, and moving the
    # wrong pid leaves the tier silently empty (this exact mistake produced an
    # enq_cache=0 run once).
    if [ "$TILT" = "0" ] && [ -w "$CG_BG/cgroup.procs" ]; then
      echo "$BASHPID" > "$CG_BG/cgroup.procs" 2>/dev/null
    fi
    while :; do
      if [ "${TILT:-0}" = "1" ]; then
        systemd-run --quiet --scope --unit "ep-mx-bg-$BLOCK-$RANDOM" -p CPUWeight=50 "$SVC" bg safe-basic "$BG_WINDOW_S"
      else
        "$SVC" bg safe-basic "$BG_WINDOW_S"
      fi >> "$d/bg.ndjson" 2>> "$d/bg.err" 9>&- || break
    done
  ) &
  BG_PID=$!
  sleep 0.2
  if ! kill -0 "$BG_PID" 2>/dev/null; then
    echo "bg worker exited immediately" >> "$d/arm.notes"
  fi
}

bg_summary() {  # dir -> median units/s over the phase
  python3 - "$1/bg.ndjson" <<'BGSTATS'
import json, sys, statistics
rows = []
for line in open(sys.argv[1], errors="replace"):
    line = line.strip()
    if not line.startswith("{"):
        continue
    try:
        rows.append(json.loads(line))
    except Exception:
        pass
vals = [r["work_units_per_sec"] for r in rows if isinstance(r.get("work_units_per_sec"), (int, float))]
print("%.2f" % (statistics.median(vals) if vals else 0.0))
BGSTATS
}

phase() {  # dir op label
  local d=$1 op=$2 label=$3 out
  out="$d/$label.json"
  "$PROBE" "$CONNS" "$RATE" "$MEASURE_S" "$op" --port "$PORT" --keypool "$KEYPOOL" \
    --slo-ms "$SLO_MS" --timeout-ms "$TIMEOUT_MS" --raw "$d/raw-$label.bin" >"$out" 2>>"$d/probe.err"
  echo "exit=$?" >> "$d/probe.status"
}

run_arm() {  # block arm
  local block=$1 arm=$2 d="$OUT/$1-$2"
  mkdir -p "$d"
  # Arm decisions and the per-block cgroup paths are resolved *before* the manifest is
  # written: the earlier order recorded the previous arm's ablation name in this arm's
  # manifest (a provenance field that lied by one block).
  TILT=0
  CLASS_ROUTING=0
  case "$arm" in
    A2) TILT=1 ;;
    Cc|Dc) CLASS_ROUTING=1 ;;
  esac
  case "$arm" in
    Cc) base_arm="C" ;;
    Dc) base_arm="D" ;;
    Dng|Dnv) base_arm="D" ;;
    *) base_arm="$arm" ;;
  esac
  export CLASS_ROUTING
  ABLATION="none"
  case "$arm" in
    Dng) ABLATION="no-service-gate" ;;
    Dnv) ABLATION="no-bg-penalty" ;;
  esac
  export ABLATION
  CG_SVC="$CG_SCOPE_ROOT/$block-svc"
  CG_BG="$CG_SCOPE_ROOT/$block-bg"
  if ! mkdir -p "$CG_SVC" "$CG_BG" 2>/dev/null; then
    echo "cannot create per-block cgroups under $CG_SCOPE_ROOT" >> "$d/block.invalid"
    stop_all
    return 1
  fi
  python3 - "$d/manifest.json" "$block" "$arm" "$RATE" "$CONNS" "$MEASURE_S" "$WARMUP_S" \
    "$KEYPOOL" "$SLO_MS" "$TIMEOUT_MS" "$SEED" "$EXECUTORD" "$EP" "$LOADER" "$PROBE" "$SVC" \
    "$POPULATE_REQUESTS" "$BG_WINDOW_S" "$BG_FLOOR_UNITS" "$BG_MAX_WAIT_MS" \
    "$FG_P99_CEILING_MS" "$HERE/redis_matrix_sp3.sh" <<'MANIFEST'
import hashlib, json, os, platform, subprocess, sys
(path, block, arm, rate, conns, measure, warmup, keypool, slo, timeout, seed,
 executord, ep, loader, probe, svc, populate, bg_window, bg_floor, bg_wait,
 fg_ceiling, harness) = sys.argv[1:23]
def sha(f):
    return hashlib.sha256(open(f, "rb").read()).hexdigest()
json.dump({
  "block": int(block), "arm": arm, "seed": int(seed),
  "started_at": subprocess.check_output(["date", "-Is"]).decode().strip(),
  "kernel": platform.release(), "cpus": os.cpu_count(), "loadavg_before": list(os.getloadavg()),
  "workload": {"generator": "redis_openloop_probe (paced, per-request raw records)",
               "redis_version": subprocess.check_output(["redis-server", "--version"]).decode().split()[-1],
               "port": int(os.environ.get("PORT", "7797")), "conns": int(conns),
               "offered_rps": float(rate), "measure_s": int(measure), "warmup_s": int(warmup),
               "keypool": int(keypool), "value_bytes": 64, "slo_ms": float(slo),
               "timeout_ms": float(timeout),
               # Every arm is initialised with the same populated keyspace; recording the
               # count is what makes "GET hit real data" auditable after the fact.
               "populate_requests": int(populate),
               "persistence": "appendonly no; save ''", "io_threads": 1},
  # The frozen online-gate parameters, per block, so a table row cannot quietly be
  # measured under a different contract than the one it is claimed under.
  "class_routing": int(os.environ.get("CLASS_ROUTING", "0")),
  "ablation": os.environ.get("ABLATION", "none"),
  "cgroups": {"service": os.environ.get("CG_SVC", ""), "background": os.environ.get("CG_BG", "")},
  "online_gate": {"bg_window_s": int(bg_window), "min_bg_work_units_per_sec": float(bg_floor),
                  "max_bg_wait_ms": float(bg_wait), "max_foreground_p99_ms": float(fg_ceiling)},
  "bg_unit": "service_load_probe bg window (same producer as the online gate)",
  "binaries": {"ep-executord": sha(executord), "ep": sha(ep), "ep-sched-loader": sha(loader),
               "schedpilotd": (sha(os.environ["E_DAEMON"])
                               if os.environ.get("E_DAEMON") and os.path.exists(os.environ["E_DAEMON"]) else None),
               "redis_openloop_probe": sha(probe), "service_load_probe": sha(svc),
               "redis_matrix_sp3.sh": sha(harness)},
  "sched_ext_at_start": (open("/sys/kernel/sched_ext/state").read().strip()
                          if os.path.exists("/sys/kernel/sched_ext/state") else "missing"),
}, open(path, "w"), indent=2, sort_keys=True)
MANIFEST
  # The policies name the cgroups this block creates, so they can only be generated
  # after the paths exist.
  arm_mode=class
  [ "$arm" = "B" ] && arm_mode=basic
  arm_expert=latency-balanced
  if [ "$arm" = "B" ]; then arm_expert=safe-basic; fi
  policy_json "$d/policy.json" "matrix-$(printf %s "$arm" | tr A-Z a-z)" "$arm_expert" human \
    "$CG_SVC" "$CG_BG" "$CLASS_ROUTING" "$ABLATION" "$arm_mode"
  policy_json "$d/policy-adaptive.json" "matrix-$(printf %s "$arm" | tr A-Z a-z)-adaptive" \
    safe-basic human "$CG_SVC" "$CG_BG" "$CLASS_ROUTING" "$ABLATION"

  start_redis "$d" || { echo "redis failed" >> "$d/block.invalid"; stop_all; return 1; }
  if [ "$TILT" = "0" ]; then
    if ! echo "$REDIS_PID" > "$CG_SVC/cgroup.procs" 2>/dev/null; then
      echo "cannot place the service in $CG_SVC" >> "$d/block.invalid"
      stop_all
      return 1
    fi
    # Read the placement back from the kernel rather than trusting the write: a policy
    # that targets a cgroup nothing is in produces a silently empty class. Compare
    # against the cgroup's own path (`0::/matrix-scope/7-svc`), not a substring of the
    # sysfs directory name -- the first version of this check grepped for the wrong
    # string and failed a placement that had actually succeeded.
    if ! grep -qx "0::${CG_SVC#/sys/fs/cgroup}" "/proc/$REDIS_PID/cgroup" 2>/dev/null; then
      echo "service not in the declared cgroup ($(cat "/proc/$REDIS_PID/cgroup" 2>/dev/null))" \
        >> "$d/block.invalid"
      stop_all
      return 1
    fi
    echo "redis_pid=$REDIS_PID cgroup=$CG_SVC" > "$d/cgroup.proof"
  else
    echo "redis_pid=$REDIS_PID placement skipped (A2 keeps its CPUWeight scope)" > "$d/cgroup.proof"
  fi
  # Same dataset in every arm, established before anything is attached so the
  # initialisation itself happens under identical conditions.
  populate_keys "$d" || { stop_all; return 1; }

  case "$arm" in
    E)
      if [ ! -x "$E_DAEMON" ]; then
        echo "arm E needs schedpilotd at E_DAEMON (have '$E_DAEMON')" >> "$d/block.invalid"
        stop_all
        return 1
      fi
      mkdir -p "$d/sp-logs"
      cat >"$d/schedpilot.conf" <<SPCONF
[targets]
# comm matching plus the block's own service cgroup: the reference classifier
# resolves names and cgroup.procs the way its P0 runs did.
process_names = redis-server
exclude_names = redis_openloop_, service_load_pr, schedpilotd, scx_schedpilot
cgroup_paths = ${CG_SVC}
[bg]
cgroup_paths = ${CG_BG}
[classifier]
alpha = 0.3
wake_hi = 500
run_lo_ns = 2000000
delay_lo_ns = 500000
ipc_hi = 1.2
mpki_hi = 10.0
lat_moderate = true
hysteresis_cycles = 10
[policy]
# classification-only: the C-equivalent of the reference control plane
adaptive = false
interval_ms = 100
classify_pmu = true
llc_affinity = true
bg_contain = true
preempt = true
[log]
dir = ${d}/sp-logs
SPCONF
      "$E_DAEMON" --config "$d/schedpilot.conf" \
        --duration $((2 * (MEASURE_S + WARMUP_S) + 90)) >"$d/daemon.log" 2>&1 &
      DAEMON=$!
      DAEMON_GRACE=1
      local attached=""
      for _ in $(seq 1 150); do
        if [ "$(cat /sys/kernel/sched_ext/state 2>/dev/null)" = "enabled" ] &&
           grep -q schedpilot /sys/kernel/sched_ext/root/ops 2>/dev/null; then
          attached=1
          break
        fi
        kill -0 "$DAEMON" 2>/dev/null || break
        sleep 0.1
      done
      if [ -z "$attached" ]; then
        echo "arm E did not attach (state=$(cat /sys/kernel/sched_ext/state 2>/dev/null) ops=$(cat /sys/kernel/sched_ext/root/ops 2>/dev/null))"           >> "$d/block.invalid"
        stop_all
        return 1
      fi
      ;;
    A1)
      # Equal observation, shadow data plane: the control plane and telemetry run, but
      # nothing attaches, so the arm's only delta versus A0 is the software stack cost.
      rm -rf "$RUN/state"; mkdir -p "$RUN/state"
      "$EXECUTORD" --state-dir "$RUN/state" --socket "$RUN/executor.sock" --skill mock \
        >"$d/daemon.log" 2>&1 &
      DAEMON=$!
      for _ in $(seq 1 100); do "$EP" status --socket "$RUN/executor.sock" >/dev/null 2>&1 && break; sleep 0.1; done
      ;;
    B|C|Cc)
      rm -rf "$RUN/state"; mkdir -p "$RUN/state"
      "$EXECUTORD" --state-dir "$RUN/state" --socket "$RUN/executor.sock" --skill sched \
        --loader "$LOADER" --sched-ctl "$CTL" >"$d/daemon.log" 2>&1 &
      DAEMON=$!
      for _ in $(seq 1 100); do "$EP" status --socket "$RUN/executor.sock" >/dev/null 2>&1 && break; sleep 0.1; done
      "$EP" apply --file "$d/policy.json" --idempotency-key "matrix-$block-$arm-$RANDOM" --ttl-ms 900000 \
        --socket "$RUN/executor.sock" >"$d/apply.json" 2>&1
      require_attach "$d" "$base_arm" || return 1
      # B is the data-plane base-cost arm: mode basic has no class semantics, so
      # unlike C/Cc there is no class routing to prove -- only the attach.
      if [ "$arm" != "B" ]; then assert_classes "$d" "$arm" || return 1; fi
      ;;
    D|Dc|Dng|Dnv)
      rm -rf "$RUN/state"; mkdir -p "$RUN/state"
      "$EXECUTORD" --state-dir "$RUN/state" --socket "$RUN/executor.sock" --skill sched \
        --loader "$LOADER" --sched-ctl "$CTL" \
        --auto-apply --auto-policy "$d/policy-adaptive.json" --auto-owner autopilot-v2 \
        --auto-min-interval-ms 3000 --auto-lease-ttl-ms 900000 --auto-dwell-ms 20000 \
        --auto-candidate-hold-ms 10000 --auto-budget-window-ms 600000 --auto-budget-max-switches 4 \
        >"$d/daemon.log" 2>&1 &
      DAEMON=$!
      for _ in $(seq 1 100); do "$EP" status --socket "$RUN/executor.sock" >/dev/null 2>&1 && break; sleep 0.1; done
      "$EP" apply --file "$d/policy-adaptive.json" --idempotency-key "matrix-$block-$arm-$RANDOM" --ttl-ms 900000 \
        --socket "$RUN/executor.sock" >"$d/apply.json" 2>&1
      require_attach "$d" "$base_arm" || return 1
      assert_classes "$d" "$arm" || return 1
      if grep -q "autopilot baseline rejected" "$d/daemon.log" 2>/dev/null; then
        echo "arm D autopilot baseline rejected: $(grep -m1 'autopilot baseline rejected' "$d/daemon.log")"           >> "$d/block.invalid"
        stop_all
        return 1
      fi
      "$EP" lease --owner autopilot-v2 --ttl-ms 900000 --socket "$RUN/executor.sock" >"$d/handover.json" 2>&1
      # D is the arm that also has to keep the service gate fed for the whole block.
      bash "$HERE/service_observer_sp3.sh" "$EP" "$RUN/executor.sock" "$SVC" "$d/observer" \
        "$BG_WINDOW_S" 1 $((2 * (MEASURE_S + WARMUP_S) + 20)) >/dev/null 2>&1 &
      OBS_PID=$!
      ;;
  esac

  run_bg "$d"
  sleep "$WARMUP_S"
  redis-cli -p "$PORT" info stats > "$d/redis.stats.before" 2>&1
  bg_before=$(bg_summary "$d")
  phase "$d" GET get
  bg_mid=$(bg_summary "$d")
  phase "$d" SET set
  bg_after=$(bg_summary "$d")
  redis-cli -p "$PORT" info stats > "$d/redis.stats.after" 2>&1
  redis-cli -p "$PORT" info commandstats > "$d/redis.commandstats" 2>&1
  cat /sys/kernel/sched_ext/state > "$d/schedext.state" 2>/dev/null || echo missing > "$d/schedext.state"
  cat /sys/kernel/sched_ext/root/ops > "$d/schedext.ops" 2>/dev/null || true
  if [ "$arm" != "A0" ] && [ "$arm" != "E" ]; then
    "$EP" status --socket "$RUN/executor.sock" > "$d/status.json" 2>&1
    [ -f "$RUN/state/journal.jsonl" ] && cp "$RUN/state/journal.jsonl" "$d/journal.jsonl"
  fi
  echo "${bg_before:-0} ${bg_mid:-0} ${bg_after:-0}" > "$d/bg.marks"
  stop_all
  cat /proc/loadavg > "$d/loadavg_after.txt" 2>/dev/null
  (cd "$d" && sha256sum ./* 2>/dev/null | grep -v SHA256SUMS > SHA256SUMS)
  echo "block $block arm $arm done $(date +%T)"
}

orders=$(python3 - "$SEED" "$BLOCKS" "$ARMS" <<'ORDER'
import random, sys
seed, blocks = int(sys.argv[1]), int(sys.argv[2])
arms = sys.argv[3].split()
rng = random.Random(seed)
for b in range(1, blocks + 1):
    order = list(arms)
    rng.shuffle(order)
    print(b, " ".join(order))
ORDER
)
export PORT
echo "$orders" > "$OUT/arm-order.txt"
echo "seed=$SEED arms=$ARMS measure=${MEASURE_S}s offered=${RATE}rps conns=${CONNS}"
cat "$OUT/arm-order.txt"

while read -r block arms; do
  [ -n "${block:-}" ] || continue
  BLOCK=$block
  for arm in $arms; do
    run_arm "$block" "$arm" || echo "block $block arm $arm failed"
  done
done <<< "$orders"

python3 - "$OUT" <<'REPORT'
import csv, json, os, statistics, sys
root = sys.argv[1]
blocks = []
for entry in sorted(os.listdir(root)):
    path = os.path.join(root, entry)
    if not os.path.isdir(path) or "-" not in entry:
        continue
    block, arm = entry.split("-", 1)
    if not os.path.exists(os.path.join(path, "manifest.json")):
        continue
    rec = {"block": int(block), "arm": arm, "invalid": os.path.exists(os.path.join(path, "block.invalid"))}
    for cmd in ("get", "set"):
        f = os.path.join(path, "%s.json" % cmd)
        raw = os.path.join(path, "raw-%s.bin" % cmd)
        if not os.path.exists(f):
            continue
        d = json.load(open(f))
        lat = os.path.getsize(raw) // 8 if os.path.exists(raw) else 0
        rec[cmd] = {"offered": d.get("offered"), "completed": d.get("completed"),
                    "goodput": d.get("goodput_at_slo_rps"), "achieved": d.get("achieved_rps"),
                    "deficit": d.get("deficit_rps"), "timeouts": d.get("timeouts"),
                    "stalls": d.get("stalls"), "cpu_per_s": d.get("cpu_per_second"),
                    "peak_inflight": d.get("peak_inflight"), "samples": lat}
    marks = os.path.join(path, "bg.marks")
    if os.path.exists(marks):
        rec["bg"] = [float(x) for x in open(marks).read().split()]
    st = os.path.join(path, "schedext.state")
    if os.path.exists(st):
        rec["attached"] = open(st).read().strip()
    blocks.append(rec)

print("blocks collected: %d" % len(blocks))
if not blocks:
    raise SystemExit(0)
for arm in sorted({b["arm"] for b in blocks}):
    for cmd in ("get", "set"):
        rows = [b[cmd] for b in blocks if b["arm"] == arm and cmd in b]
        if not rows:
            continue
        gp = [r["goodput"] for r in rows]
        ach = [r["achieved"] for r in rows]
        print("%s %s n=%d goodput@SLO median %.0f rps | achieved %.0f | timeouts %s stalls %s cpu/s %.2f | samples %s | attach %s" % (
            arm, cmd, len(rows), statistics.median(gp), statistics.median(ach),
            sum(r["timeouts"] or 0 for r in rows), sum(r["stalls"] or 0 for r in rows),
            statistics.median([r["cpu_per_s"] or 0 for r in rows]),
            sum(r["samples"] or 0 for r in rows),
            ",".join(sorted({str(b.get("attached")) for b in blocks if b["arm"] == arm}))))
    bg = [statistics.median(b["bg"]) for b in blocks if b["arm"] == arm and b.get("bg")]
    if bg:
        print("   %s bg units/s (median of window medians) %s" % (arm, "%.1f" % statistics.median(bg)))
print()
print("NOTE block counts here are a harness smoke, not the flagship sample; the table is")
print("recomputable from raw-*.bin (protocol: raw records must rebuild the numbers).")
REPORT
