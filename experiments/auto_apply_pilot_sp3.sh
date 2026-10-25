#!/usr/bin/env bash
# EulerPilot v2 -- autopilot pilot on real sched_ext (SP3, root).
#
# Shows the three properties the autopilot is supposed to have:
#   1. it never acts until an operator hands it the lease (--owner autopilot-v2);
#   2. while holding the lease it moves an in-place policy to the routed expert
#      through the normal apply path (rollback then apply, journal, generation);
#   3. the moment anybody else takes the lease it stops, and says why.
#
# Load target: enough runnable threads that PSI some avg10 crosses the router's
# 20.0 threshold, because that branch routes to latency-balanced and the SLO gate
# only requires PSI to be valid for that expert. throughput-balanced would be
# denied under this same pressure, so it is not the target here.
#
# Not part of ctest. Run manually on an idle sched_ext host.
set -uo pipefail

EXECUTORD="${1:?usage: auto_apply_pilot_sp3.sh <ep-executord> <ep> <ep-sched-loader> [out-dir]}"
EP="${2:?usage: auto_apply_pilot_sp3.sh <ep-executord> <ep> <ep-sched-loader> [out-dir]}"
LOADER="${3:?usage: auto_apply_pilot_sp3.sh <ep-executord> <ep> <ep-sched-loader> [out-dir]}"
OUT="${4:-/root/epv2-evidence/auto-pilot}"
CTL="$(dirname "$LOADER")/ep-schedctl"
PIN_ROOT=/sys/fs/bpf/eulerpilot-v2

[ "$(id -u)" = 0 ] || { echo "requires root"; exit 77; }
[ -x "$EXECUTORD" ] && [ -x "$EP" ] && [ -x "$LOADER" ] && [ -x "$CTL" ] || { echo "binaries missing"; exit 77; }
[ "$(cat /sys/kernel/sched_ext/state 2>/dev/null)" = "disabled" ] || { echo "sched_ext not idle"; exit 1; }
[ -z "$(pgrep -x ep-executord)" ] || { echo "executor already running"; exit 1; }
[ -z "$(pgrep -x schedpilotd)" ] || { echo "schedpilotd is running"; exit 1; }

mkdir -p "$OUT"
RUN=/tmp/ep-auto-pilot
rm -rf "$RUN"; mkdir -p "$RUN/state"
SOCK="$RUN/executor.sock"
BASELINE="$RUN/baseline.json"
DAEMON=""
LOAD_PIDS=()

cleanup() {
  local rc=$?
  for pid in "${LOAD_PIDS[@]:-}"; do kill -KILL "$pid" 2>/dev/null; done
  wait 2>/dev/null
  if [ -n "$DAEMON" ] && kill -0 "$DAEMON" 2>/dev/null; then
    kill -TERM "$DAEMON" 2>/dev/null
    wait "$DAEMON" 2>/dev/null
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
  [ "$rc" -eq 0 ] || echo "evidence retained: $OUT $RUN"
}
trap cleanup EXIT

python3 - "$BASELINE" <<'PY'
import json, sys
json.dump({
  "apiVersion": "eulerpilot.io/v1alpha1", "kind": "SchedulingPolicy",
  "metadata": {"name": "autopilot-pilot"},
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
               "policy_ttl_ms": 120000, "quiet_max_regression_pct": 5,
               "evaluation_min_bg_progress_ratio": 0.5, "bg_work_unit": "requests",
               "bg_progress_window_ms": 5000, "min_bg_work_units_per_sec": 1,
               "max_bg_wait_ms": 1000, "fallback": "verified-safe-profile",
               "allow_affinity_change": False}
  }
}, open(sys.argv[1], "w"))
PY

# Autopilot on, but nobody has handed it the lease yet.
"$EXECUTORD" --state-dir "$RUN/state" --socket "$SOCK" --skill sched \
  --loader "$LOADER" --sched-ctl "$CTL" \
  --auto-apply --auto-policy "$BASELINE" --auto-owner autopilot-v2 \
  --auto-min-interval-ms 4000 --auto-lease-ttl-ms 30000 >"$RUN/daemon.log" 2>&1 &
DAEMON=$!
for _ in $(seq 1 100); do "$EP" status --socket "$SOCK" >/dev/null 2>&1 && break; sleep 0.1; done

status_field() {
  python3 -c "
import json, sys
d = json.loads(sys.stdin.read())
node = d
for key in sys.argv[1].split('.'):
    node = node.get(key) if isinstance(node, dict) else None
print(node)
" "$1"
}

"$EP" apply --file "$BASELINE" --idempotency-key auto-pilot-apply-1 --socket "$SOCK" \
  >"$OUT/01-human-apply.json" 2>&1
cp "$RUN/daemon.log" "$OUT/01-daemon.log"
grep -E "autopilot|lease acquire failed" "$RUN/daemon.log" | tail -3 > "$OUT/01-autopilot-lines.txt" || true
human_status=$("$EP" status --socket "$SOCK")
echo "$human_status" > "$OUT/01-status-before-handover.json"
python3 -c "import json;d=json.load(open('$OUT/01-human-apply.json'));print('HUMAN_APPLY='+d['status'])"
echo "$human_status" | status_field auto_apply.active_expert | sed 's/^/BEFORE_HANDOVER active_expert=/;'
echo "$human_status" | status_field auto_apply.last_hold | sed 's/^/BEFORE_HANDOVER last_hold=/;'
gen_before=$(echo "$human_status" | status_field generation)

# Property 1: the handover. Nothing moves until the lease names the autopilot.
"$EP" lease --owner autopilot-v2 --ttl-ms 30000 --socket "$SOCK" >"$OUT/02-handover.json" 2>&1

CPUS=$(nproc)
for _ in $(seq 1 $((CPUS * 2))); do
  (while :; do :; done) &
  LOAD_PIDS+=("$!")
done
echo "load_threads=$((CPUS * 2)) on $CPUS cpus"

# Property 2: it moves the in-place policy to the routed expert.
switched=""
for _ in $(seq 1 30); do
  sleep 2
  st=$("$EP" status --socket "$SOCK")
  exp=$(echo "$st" | status_field auto_apply.active_expert)
  hold=$(echo "$st" | status_field auto_apply.last_hold)
  psi=$("$EP" features --socket "$SOCK" | status_field metrics.psi_cpu_some_avg10.avg)
  echo "  poll active_expert=$exp psi_avg10=$psi last_hold=$hold"
  if [ "$exp" != "safe-basic" ] && [ -n "$exp" ] && [ "$exp" != "None" ]; then
    switched="$exp"
    break
  fi
done
"$EP" status --socket "$SOCK" > "$OUT/03-status-after-switch.json"
gen_after=$(python3 -c "import json;print(json.load(open('$OUT/03-status-after-switch.json'))['generation'])")
grep -E "autopilot" "$RUN/daemon.log" | tail -6 > "$OUT/03-autopilot-lines.txt" || true

if [ -n "$switched" ]; then
  echo "PROPERTY2_SWITCHED=to $switched (generation $gen_before -> $gen_after)"
else
  echo "PROPERTY2_SWITCHED=no (autopilot never moved the policy)"
fi

# Property 3: an operator reclaiming the lease stops it, and it says why.
"$EP" lease --owner cli-operator-reclaim --ttl-ms 30000 --socket "$SOCK" >"$OUT/04-reclaim.json" 2>&1
gen_locked=$(python3 -c "import json;print(json.load(open('$OUT/04-reclaim.json'))['observed_generation'])")
# Push the features the other way so a naive implementation would want to move back.
for pid in "${LOAD_PIDS[@]:-}"; do kill -KILL "$pid" 2>/dev/null; done
wait 2>/dev/null
LOAD_PIDS=()
sleep 14
"$EP" status --socket "$SOCK" > "$OUT/05-status-after-reclaim.json"
gen_reclaim=$(python3 -c "import json;print(json.load(open('$OUT/05-status-after-reclaim.json'))['generation'])")
hold=$(python3 -c "import json;print(json.load(open('$OUT/05-status-after-reclaim.json'))['auto_apply']['last_hold'])")
exp=$(python3 -c "import json;print(json.load(open('$OUT/05-status-after-reclaim.json'))['auto_apply']['active_expert'])")
echo "PROPERTY3_HELD_BY_OPERATOR last_hold=$hold active_expert=$exp generation $gen_locked -> $gen_reclaim"
if [ "$gen_reclaim" = "$gen_locked" ]; then
  echo "PROPERTY3_STOPPED=yes"
else
  echo "PROPERTY3_STOPPED=no (generation still advanced after reclaim)"
fi
grep -E "autopilot" "$RUN/daemon.log" | tail -8 > "$OUT/05-autopilot-lines.txt" || true
