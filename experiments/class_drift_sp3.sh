#!/usr/bin/env bash
# Class-map drift sampling on a real kernel (kernel-integration layer).
#
# S12 proved an out-of-band cfg write is corrected every refresh cycle; the
# class map had no equivalent, and the refresh budget cannot afford one helper
# call per classified task. Since 2026-10-25 refresh() samples one managed
# entry per cycle (full coverage in N cycles). This script is the falsifiable
# real-machine check: overwrite and delete a managed entry out-of-band and
# watch the executor put it back.
#
#   class_drift_sp3.sh <ep-executord> <ep> <ep-sched-loader> [output-dir]
set -euo pipefail

EXECUTORD="${1:?usage: class_drift_sp3.sh <ep-executord> <ep> <ep-sched-loader> [out]}"
EP="${2:?}"
LOADER="${3:?}"
OUT="${4:-$(mktemp -d /tmp/ep-class-drift.XXXXXX)}"

skip() { echo "[skip] $*"; exit 77; }
[ "$(id -u)" = "0" ] || skip "requires root"
[ -x "$LOADER" ] || skip "loader not executable"
CTL="$(dirname "$LOADER")/ep-schedctl"
[ -x "$CTL" ] || skip "ep-schedctl not executable: $CTL"
[ "$(cat /sys/kernel/sched_ext/state 2>/dev/null)" = "disabled" ] ||
  skip "sched_ext is not idle"
if [ -d /sys/fs/bpf/eulerpilot-v2 ] &&
   [ -n "$(ls -A /sys/fs/bpf/eulerpilot-v2 2>/dev/null)" ]; then
  skip "v2 pin root already has entries"
fi

STATE="$OUT/state"
SOCK="$STATE/executor.sock"
mkdir -p "$OUT" "$STATE"
DAEMON=""
BG_PID=""

cleanup() {
  local rc=$?
  [ -n "$DAEMON" ] && kill -TERM "$DAEMON" 2>/dev/null || true
  [ -n "$BG_PID" ] && kill -TERM "$BG_PID" 2>/dev/null || true
  wait 2>/dev/null || true
  if [ "$rc" -ne 0 ]; then
    echo "[FAIL] evidence retained in $OUT" >&2
  fi
  exit "$rc"
}
trap cleanup EXIT

fail() { echo "[FAIL] $*" >&2; [ -f "$OUT/daemon.log" ] && tail -20 "$OUT/daemon.log" >&2; exit 1; }
field() { python3 -c 'import json,sys; print(json.loads(sys.argv[1]).get(sys.argv[2],""))' "$1" "$2"; }
class_of() {  # <pid>
  python3 -c 'import json,sys
r = json.loads(sys.argv[1])
print(r.get("class","-") if r.get("present") else "absent")' \
    "$("$CTL" class-get --pin-dir "$PINDIR" --pid "$1")"
}
wait_class() { # <pid> <expected> <seconds>
  local deadline=$((SECONDS + $3))
  while [ $SECONDS -lt $deadline ]; do
    [ "$(class_of "$1")" = "$2" ] && return 0
    sleep 0.5
  done
  return 1
}

sleep 300 >/dev/null 2>&1 &
BG_PID=$!
POLICY="$OUT/policy.json"
BG_PID="$BG_PID" python3 - "$POLICY" <<'PY'
import json, os, sys
json.dump({
    "apiVersion": "eulerpilot.io/v1alpha1",
    "kind": "SchedulingPolicy",
    "metadata": {"name": "class-drift-check"},
    "spec": {
        "scope": "scheduler-instance",
        "mode": "class",
        "expert": "latency-balanced",
        "source": "human",
        "targets": [
            {"kind": "cgroup", "path": "/sys/fs/cgroup/system.slice",
             "role": "FOREGROUND", "role_source": "explicit"},
            {"kind": "pid", "pid": int(os.environ["BG_PID"]),
             "role": "BACKGROUND", "role_source": "explicit", "class": "BG"},
        ],
        "parameters": {
            "lat_slice_ns": 2000000, "comp_slice_ns": 4000000,
            "cache_slice_ns": 4000000, "bg_slice_ns": 2000000,
            "preempt_threshold_ns": 500000, "bg_vtime_pct": 200,
            "llc_affinity": False,
        },
        "safety": {
            "min_dwell_ms": 5000, "max_observation_age_ms": 1000,
            "heartbeat_period_ms": 250, "heartbeat_timeout_ms": 2000,
            "policy_ttl_ms": 600000, "quiet_max_regression_pct": 5,
            "evaluation_min_bg_progress_ratio": 0.5,
            "bg_work_unit": "requests", "bg_progress_window_ms": 5000,
            "min_bg_work_units_per_sec": 1, "max_foreground_p99_ms": 50,
            "max_bg_wait_ms": 1000, "fallback": "verified-safe-profile",
            "allow_affinity_change": False,
        },
    },
}, open(sys.argv[1], "w"))
PY

echo "[class-drift] starting the executor and applying the policy"
"$EXECUTORD" --state-dir "$STATE" --socket "$SOCK" --skill sched \
  --loader "$LOADER" --sched-ctl "$CTL" >"$OUT/daemon.log" 2>&1 9>&- &
DAEMON=$!
for _ in $(seq 1 100); do "$EP" status --socket "$SOCK" >/dev/null 2>&1 && break; sleep 0.1; done
resp="$("$EP" apply --file "$POLICY" --idempotency-key idem-drift-0001 --socket "$SOCK")" ||
  fail "apply failed: $resp"
[ "$(field "$resp" status)" = "VERIFIED" ] || fail "apply not VERIFIED: $resp"
INSTANCE="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["instance_id"])' "$STATE/state.json")"
PINDIR="/sys/fs/bpf/eulerpilot-v2/$INSTANCE"
[ "$(cat /sys/kernel/sched_ext/state)" = "enabled" ] || fail "not attached"
[ "$(class_of "$BG_PID")" = "BG" ] || fail "declared class not in the kernel map"

echo "[class-drift] scenario 1: out-of-band overwrite BG -> COMP"
"$CTL" class-set --pin-dir "$PINDIR" --pid "$BG_PID" --class COMP --confidence 900 >/dev/null ||
  fail "out-of-band class-set failed"
[ "$(class_of "$BG_PID")" = "COMP" ] || fail "could not simulate the drift"
wait_class "$BG_PID" BG 20 || fail "drifted entry was not corrected (still $(class_of "$BG_PID"))"
grep -q "class drift corrected: pid $BG_PID wanted BG, kernel had COMP" "$OUT/daemon.log" ||
  fail "the correction happened without being logged"
echo "[class-drift] corrected back to BG and reported"

echo "[class-drift] scenario 2: out-of-band delete"
"$CTL" class-del --pin-dir "$PINDIR" --pid "$BG_PID" >/dev/null ||
  fail "out-of-band class-del failed"
[ "$(class_of "$BG_PID")" = "absent" ] || fail "could not simulate the deletion"
wait_class "$BG_PID" BG 20 || fail "deleted entry was not re-asserted (still $(class_of "$BG_PID"))"
grep -q "class drift corrected: pid $BG_PID wanted BG, kernel had no entry" "$OUT/daemon.log" ||
  fail "the re-assertion happened without being logged"
echo "[class-drift] re-asserted and reported"

echo "[class-drift] rollback"
ACTION="$(field "$resp" action_id)"
rb="$("$EP" rollback --of "$ACTION" --transaction "$(field "$resp" transaction_id)" \
  --snapshot "$(field "$resp" snapshot_id)" --policy-sha256 "$(field "$resp" policy_sha256)" \
  --socket "$SOCK")" || fail "rollback failed"
[ "$(field "$rb" status)" = "ROLLED_BACK" ] || fail "rollback not ROLLED_BACK: $rb"
[ "$(cat /sys/kernel/sched_ext/state)" = "disabled" ] || fail "still attached after rollback"
[ "$(class_of "$BG_PID")" = "absent" ] || fail "class entry outlived the rollback"
echo "[class-drift] passing"
