#!/usr/bin/env bash
# A/B/C liveness (no-stall) check. This is NOT a performance measurement:
# it only asserts that a CPU-bound task makes progress under fair, basic and
# class scheduling, and that sched_ext state transitions are clean.
set -euo pipefail

EXECUTORD="${1:?usage: abc_nostall.sh <ep-executord> <ep> <loader>}"
EP="${2:?usage: abc_nostall.sh <ep-executord> <ep> <loader>}"
LOADER="${3:?usage: abc_nostall.sh <ep-executord> <ep> <loader>}"
CTL="$(dirname "$LOADER")/ep-schedctl"

skip() { echo "[skip] $*"; exit 77; }
[ "$(id -u)" = "0" ] || skip "requires root"
[ -x "$LOADER" ] || skip "loader not executable"
[ -x "$CTL" ] || skip "ep-schedctl not executable"
[ -d /sys/kernel/sched_ext ] || skip "sched_ext unavailable"
[ "$(cat /sys/kernel/sched_ext/state 2>/dev/null)" = "disabled" ] || skip "sched_ext is not idle"
if [ -d /sys/fs/bpf/eulerpilot-v2 ] &&
   [ -n "$(ls -A /sys/fs/bpf/eulerpilot-v2 2>/dev/null)" ]; then
  skip "pin root is not empty"
fi

TMP="$(mktemp -d /tmp/ep-abc.XXXXXX)"
STATE="$TMP/state"
SOCK="$TMP/executor.sock"
DAEMON_PID=""
BUSY_PID=""
trap 'cleanup' EXIT

cleanup() {
  local result=$?
  if [ -n "$BUSY_PID" ]; then
    kill -TERM "$BUSY_PID" 2>/dev/null || true
    wait "$BUSY_PID" 2>/dev/null || true
  fi
  if [ -n "$DAEMON_PID" ] && kill -0 "$DAEMON_PID" 2>/dev/null; then
    kill -TERM "$DAEMON_PID" 2>/dev/null || true
    wait "$DAEMON_PID" 2>/dev/null || true
  fi
  # The executor owns its instance resources. Do not delete a shared pin root.
  if [ "$result" -eq 0 ]; then
    rm -rf "$TMP"
  else
    echo "[FAIL] liveness evidence retained: $TMP" >&2
  fi
}

field() {
  python3 -c 'import json,sys; print(json.loads(sys.argv[1]).get(sys.argv[2],""))' "$1" "$2"
}

wait_state() {
  local expected="$1"
  for _ in $(seq 1 100); do
    [ "$(cat /sys/kernel/sched_ext/state 2>/dev/null)" = "$expected" ] && return 0
    sleep 0.05
  done
  return 1
}

start_daemon() {
  rm -f "$SOCK"
  "$EXECUTORD" --state-dir "$STATE" --socket "$SOCK" --skill sched \
    --loader "$LOADER" --sched-ctl "$CTL" >"$TMP/daemon.log" 2>&1 &
  DAEMON_PID=$!
  for _ in $(seq 1 100); do
    "$EP" status --socket "$SOCK" >/dev/null 2>&1 && return 0
    kill -0 "$DAEMON_PID" 2>/dev/null || break
    sleep 0.05
  done
  echo "[FAIL] daemon did not start" >&2
  exit 1
}

make_policy() {
  local mode="$1" file="$2"
  python3 - "$mode" "$file" <<'PY'
import json, sys
mode, path = sys.argv[1], sys.argv[2]
policy = {
    "apiVersion": "eulerpilot.io/v1alpha1",
    "kind": "SchedulingPolicy",
    "metadata": {"name": "abc-" + mode},
    "spec": {
        "scope": "scheduler-instance",
        "mode": mode,
        "expert": "latency-balanced",
        "source": "human",
        "targets": [{"kind": "cgroup", "path": "/sys/fs/cgroup/system.slice",
                     "role": "FOREGROUND", "role_source": "explicit"}],
        "parameters": {
            "lat_slice_ns": 2000000, "comp_slice_ns": 4000000,
            "cache_slice_ns": 4000000, "bg_slice_ns": 2000000,
            "preempt_threshold_ns": 500000, "bg_vtime_pct": 200,
            "llc_affinity": False,
        },
        "safety": {
            "min_dwell_ms": 5000, "max_observation_age_ms": 1000,
            "heartbeat_period_ms": 250, "heartbeat_timeout_ms": 2000,
            "policy_ttl_ms": 60000, "quiet_max_regression_pct": 5,
            "evaluation_min_bg_progress_ratio": 0.5, "bg_work_unit": "requests",
            "bg_progress_window_ms": 5000, "min_bg_work_units_per_sec": 1,
            "max_bg_wait_ms": 1000, "fallback": "verified-safe-profile",
            "allow_affinity_change": False,
        },
    },
}
json.dump(policy, open(path, "w"))
PY
}

busy_progress() {
  local label="$1"
  bash -c 'while :; do :; done' &
  local pid=$!
  BUSY_PID="$pid"
  sleep 0.3
  local t0 t1
  t0="$(awk '{print $14}' "/proc/$pid/stat")"
  sleep 2
  t1="$(awk '{print $14}' "/proc/$pid/stat")"
  kill -TERM "$pid" 2>/dev/null || true
  wait "$pid" 2>/dev/null || true
  BUSY_PID=""
  if [ "$t1" -le "$t0" ]; then
    echo "[FAIL] $label: no CPU progress (t0=$t0 t1=$t1)" >&2
    exit 1
  fi
  printf '%s\n' "$label" "$t0" "$t1"
}

emit() {
  local arm="$1" state="$2" t0="$3" t1="$4"
  python3 - "$arm" "$state" "$t0" "$t1" <<'PY'
import json, sys
arm, state, t0, t1 = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
print(json.dumps({"arm": arm, "state": state, "utime_before": t0,
                  "utime_after": t1, "progress_ticks": t1 - t0}))
PY
}

start_daemon

echo "[abc] fair arm (sched_ext disabled)"
[ "$(cat /sys/kernel/sched_ext/state)" = "disabled" ] || { echo "[FAIL] fair arm not idle" >&2; exit 1; }
busy_progress fair > "$TMP/fair.progress"
mapfile -t fair < "$TMP/fair.progress"
emit fair disabled "${fair[1]}" "${fair[2]}"

for mode in basic class; do
  echo "[abc] $mode arm"
  make_policy "$mode" "$TMP/$mode.json"
  resp="$("$EP" apply --file "$TMP/$mode.json" --idempotency-key "idem-abc-$mode" --socket "$SOCK")"
  [ "$(field "$resp" status)" = "VERIFIED" ] || { echo "[FAIL] $mode apply: $resp" >&2; exit 1; }
  wait_state enabled || { echo "[FAIL] $mode did not enable" >&2; exit 1; }
  busy_progress "$mode" > "$TMP/$mode.progress"
  mapfile -t arm < "$TMP/$mode.progress"
  emit "$mode" enabled "${arm[1]}" "${arm[2]}"
  "$EP" rollback --of "$(field "$resp" action_id)" \
    --transaction "$(field "$resp" transaction_id)" \
    --snapshot "$(field "$resp" snapshot_id)" \
    --policy-sha256 "$(field "$resp" policy_sha256)" --socket "$SOCK" >/dev/null ||
    { echo "[FAIL] $mode rollback failed" >&2; exit 1; }
  wait_state disabled || { echo "[FAIL] $mode stayed enabled" >&2; exit 1; }
done

echo "[abc] all arms made progress; sched_ext is disabled"
