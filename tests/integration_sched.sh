#!/usr/bin/env bash
# Real sched_ext integration test for the P1 sched-ref skill (v2 loader).
# Run only on a host/VM where the v2 sched loader can attach. Otherwise exit 77
# so CTest marks the test as skipped.
set -euo pipefail

EXECUTORD="${1:?usage: integration_sched.sh <ep-executord> <ep> [loader]}"
EP="${2:?usage: integration_sched.sh <ep-executord> <ep> [loader]}"
LOADER="${3:-}"

skip() {
  echo "[skip] $*"
  exit 77
}

[ "$(id -u)" = "0" ] || skip "requires root"
[ -n "$LOADER" ] && [ -x "$LOADER" ] || skip "sched loader not provided/executable: '$LOADER'"
CTL="$(dirname "$LOADER")/ep-schedctl"
[ -x "$CTL" ] || skip "ep-schedctl not executable: '$CTL'"
[ -d /sys/kernel/sched_ext ] || skip "sched_ext is not available"
[ "$(cat /sys/kernel/sched_ext/state 2>/dev/null)" = "disabled" ] ||
  skip "sched_ext is not idle"
if [ -d /sys/fs/bpf/eulerpilot-v2 ] &&
   [ -n "$(ls -A /sys/fs/bpf/eulerpilot-v2 2>/dev/null)" ]; then
  skip "v2 pin root already has entries; not touching a foreign environment"
fi

TMP="$(mktemp -d /tmp/ep-sched-int.XXXXXX)"
STATE="$TMP/state"
SOCK="$TMP/executor.sock"
DAEMON_PID=""
POLICY="$TMP/policy.json"

cat >"$POLICY" <<'JSON'
{
  "apiVersion": "eulerpilot.io/v1alpha1",
  "kind": "SchedulingPolicy",
  "metadata": {"name": "p1-integration"},
  "spec": {
    "scope": "scheduler-instance",
    "mode": "class",
    "expert": "latency-balanced",
    "source": "human",
    "targets": [
      {"kind": "cgroup", "path": "/sys/fs/cgroup/system.slice",
       "role": "FOREGROUND", "role_source": "explicit"}
    ],
    "parameters": {
      "lat_slice_ns": 2000000,
      "comp_slice_ns": 4000000,
      "cache_slice_ns": 4000000,
      "bg_slice_ns": 2000000,
      "preempt_threshold_ns": 500000,
      "bg_vtime_pct": 200,
      "llc_affinity": false
    },
    "safety": {
      "min_dwell_ms": 5000,
      "max_observation_age_ms": 1000,
      "heartbeat_period_ms": 250,
      "heartbeat_timeout_ms": 2000,
      "policy_ttl_ms": 60000,
      "quiet_max_regression_pct": 5,
      "evaluation_min_bg_progress_ratio": 0.5,
      "bg_work_unit": "requests",
      "bg_progress_window_ms": 5000,
      "min_bg_work_units_per_sec": 1,
      "max_bg_wait_ms": 1000,
      "fallback": "verified-safe-profile",
      "allow_affinity_change": false
    }
  }
}
JSON

fail() {
  echo "[FAIL] $*" >&2
  if [ -f "$TMP/daemon.log" ]; then tail -n 40 "$TMP/daemon.log" >&2 || true; fi
  if [ -f "$STATE/sched-loader.log" ]; then tail -n 40 "$STATE/sched-loader.log" >&2 || true; fi
  exit 1
}

field() {
  python3 -c 'import json,sys; d=json.loads(sys.argv[1]); v=d.get(sys.argv[2],""); print(v)' \
    "$1" "$2"
}

stop_owned_loader() {
  if [ -f "$STATE/sched-runtime.json" ]; then
    local pid
    pid="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1])).get("pid",0))' \
      "$STATE/sched-runtime.json" 2>/dev/null || echo 0)"
    if [ "$pid" != "0" ] && kill -0 "$pid" 2>/dev/null; then
      kill -TERM "$pid" 2>/dev/null || true
      for _ in $(seq 1 60); do kill -0 "$pid" 2>/dev/null || break; sleep 0.05; done
      kill -KILL "$pid" 2>/dev/null || true
    fi
  fi
  if [ "$(cat /sys/kernel/sched_ext/state 2>/dev/null)" = "enabled" ]; then
    local ops
    ops="$(cat /sys/kernel/sched_ext/root/ops 2>/dev/null || true)"
    case "$ops" in
      *eulerpilot*)
        echo "[cleanup] eulerpilot still attached" >&2
        ;;
    esac
  fi
  # Keep shared pins and failed recovery evidence; only the owner may clean up.
}

cleanup() {
  local result=$?
  if [ -n "$DAEMON_PID" ] && kill -0 "$DAEMON_PID" 2>/dev/null; then
    kill -TERM "$DAEMON_PID" 2>/dev/null || true
    wait "$DAEMON_PID" 2>/dev/null || true
  fi
  stop_owned_loader
  if [ "$result" -eq 0 ]; then rm -rf "$TMP"; else echo "[FAIL] evidence retained: $TMP" >&2; fi
}
trap cleanup EXIT

start_daemon() {
  rm -f "$SOCK"
  "$EXECUTORD" --state-dir "$STATE" --socket "$SOCK" --skill sched-ref --loader "$LOADER" \
    --sched-ctl "$CTL" >>"$TMP/daemon.log" 2>&1 &
  DAEMON_PID=$!
  for _ in $(seq 1 100); do
    if "$EP" status --socket "$SOCK" >/dev/null 2>&1; then return 0; fi
    kill -0 "$DAEMON_PID" 2>/dev/null || break
    sleep 0.05
  done
  fail "daemon did not answer status on $SOCK"
}

stop_daemon() {
  if [ -n "$DAEMON_PID" ]; then
    kill -TERM "$DAEMON_PID" 2>/dev/null || true
    wait "$DAEMON_PID" 2>/dev/null || true
    DAEMON_PID=""
  fi
}

wait_state() {
  local expected="$1"
  for _ in $(seq 1 100); do
    [ "$(cat /sys/kernel/sched_ext/state 2>/dev/null)" = "$expected" ] && return 0
    sleep 0.05
  done
  return 1
}

echo "[integration-sched] loader=$LOADER"

start_daemon
status="$("$EP" status --socket "$SOCK")" || fail "status failed"
[ "$(field "$status" status)" = "OK" ] || fail "initial status not OK: $status"
INSTANCE="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["instance_id"])' "$STATE/state.json")"
PINDIR="/sys/fs/bpf/eulerpilot-v2/$INSTANCE"
echo "[integration-sched] instance=$INSTANCE pin_dir=$PINDIR"

echo "[integration-sched] validate policy"
validation="$("$EP" validate --file "$POLICY" --socket "$SOCK")" || fail "policy validation failed"
[ "$(field "$validation" status)" = "OK" ] || fail "validate not OK: $validation"
[ "$(field "$validation" mode)" = "class" ] || fail "validated mode is not class"
[ "$(field "$validation" expert)" = "latency-balanced" ] || fail "validated expert mismatch"

echo "[integration-sched] apply"
resp="$("$EP" apply --file "$POLICY" --idempotency-key idem-sched-int-0001 --socket "$SOCK")" ||
  fail "apply failed"
[ "$(field "$resp" status)" = "VERIFIED" ] || fail "apply not VERIFIED: $resp"
[ "$(field "$resp" observed_generation)" = "1" ] || fail "generation is not 1"
[ "$(cat /sys/kernel/sched_ext/state)" = "enabled" ] || fail "sched_ext not enabled after apply"
grep -q eulerpilot /sys/kernel/sched_ext/root/ops || fail "ops does not contain eulerpilot"
[ -f "$PINDIR"/cfg ] || fail "cfg pin missing"
[ -f "$STATE/sched-runtime.json" ] || fail "runtime marker missing"

echo "[integration-sched] cfg parameters"
cfg_json="$("$CTL" get --pin-dir "$PINDIR")" || fail "schedctl get failed"
python3 - "$cfg_json" <<'PY'
import json, sys
reply = json.loads(sys.argv[1])
assert reply.get("ok") is True, reply
cfg = reply["cfg"]
assert cfg["lat_slice_ns"] == 2000000, cfg
assert cfg["comp_slice_ns"] == 4000000, cfg
assert cfg["cache_slice_ns"] == 4000000, cfg
assert cfg["bg_slice_ns"] == 2000000, cfg
assert cfg["preempt_threshold_ns"] == 500000, cfg
assert cfg["bg_vtime_pct"] == 200, cfg
assert cfg["llc_affinity"] is False, cfg
assert cfg["generation"] >= 2, cfg
print("[integration-sched] cfg generation", cfg["generation"])
PY

echo "[integration-sched] rollback"
ACTION="$(field "$resp" action_id)"
SNAPSHOT="$(field "$resp" snapshot_id)"
TXN="$(field "$resp" transaction_id)"
DIGEST="$(field "$resp" policy_sha256)"
rb="$("$EP" rollback --of "$ACTION" --transaction "$TXN" --snapshot "$SNAPSHOT" \
  --policy-sha256 "$DIGEST" --socket "$SOCK")" || fail "rollback failed"
[ "$(field "$rb" status)" = "ROLLED_BACK" ] || fail "rollback not ROLLED_BACK: $rb"
wait_state disabled || fail "sched_ext still enabled after rollback"
[ ! -e "$PINDIR" ] || fail "pins were not removed"
[ ! -f "$STATE/sched-runtime.json" ] || fail "runtime marker was not removed"

echo "[integration-sched] crash + orphan recovery"
resp2="$("$EP" apply --file "$POLICY" --idempotency-key idem-sched-int-0002 --socket "$SOCK")" ||
  fail "second apply failed"
[ "$(field "$resp2" status)" = "VERIFIED" ] || fail "second apply not VERIFIED: $resp2"
[ "$(cat /sys/kernel/sched_ext/state)" = "enabled" ] || fail "second apply did not attach"
kill -KILL "$DAEMON_PID" 2>/dev/null || true
wait "$DAEMON_PID" 2>/dev/null || true
DAEMON_PID=""
wait_state disabled || fail "orphan loader stayed attached after executor SIGKILL"
[ -e "$PINDIR" ] || fail "pins unexpectedly disappeared (test assumption broken)"

start_daemon
status="$("$EP" status --socket "$SOCK")" || fail "status after restart failed"
[ "$(field "$status" status)" = "OK" ] || fail "status after restart is not OK: $status"
[ "$(field "$status" degraded)" = "False" ] || fail "restart is degraded after orphan recovery"
[ ! -e "$PINDIR" ] || fail "orphan pins were not cleaned on restart"
[ ! -f "$STATE/sched-runtime.json" ] || fail "runtime marker survived restart"
[ "$(field "$status" generation)" = "3" ] || fail "generation is not 3 after restart"

stop_daemon
echo "[integration-sched] all checks passed"
