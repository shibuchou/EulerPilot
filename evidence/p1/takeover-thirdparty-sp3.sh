#!/usr/bin/env bash
# EulerPilot v2 -- third-party takeover probe on real sched_ext (SP3, root).
#
# A runs the production path (--skill sched: the loader attaches once, hands the
# struct_ops link FD over SCM_RIGHTS to the executor and exits). A SchedPilot
# reference loader then registers as ops "schedpilot", which makes the kernel
# run it and leaves A's link FD holding a detached struct_ops instance.
#
# Observed questions, not assertions that abort:
#   D1 does A notice the takeover at all, and how fast?
#   D2 which check fires first: ops identity or enable_seq?
#   D3 does A's rollback disturb the scheduler that is actually in place?
#   D4 does A only remove its own pins?
#   D5 does the machine end detached with no leftover v2 pins?
#
# This deliberately overlaps two schedulers and only uses the reference loader,
# never schedpilotd. Approved 2026-10-22 for this probe; not a delivery path.
set -uo pipefail

B=/root/epv2-build
REF=/root/p0/schedpilot-0aeca0c/build/scx_schedpilot
A_DIR=/tmp/ep-take3-A
POLICY=/tmp/ep-take3-policy.json
PIN_ROOT=/sys/fs/bpf/eulerpilot-v2
REF_PIN_ROOT=/sys/fs/bpf/schedpilot
A_PID=""
REF_PID=""

cleanup() {
  if [ -n "$REF_PID" ] && kill -0 "$REF_PID" 2>/dev/null; then
    kill -TERM "$REF_PID" 2>/dev/null
    wait "$REF_PID" 2>/dev/null
  fi
  for _ in $(seq 1 60); do
    [ "$(cat /sys/kernel/sched_ext/state 2>/dev/null)" = "disabled" ] && break
    sleep 0.5
  done
  if [ -n "$A_PID" ] && kill -0 "$A_PID" 2>/dev/null; then
    kill -TERM "$A_PID" 2>/dev/null
    wait "$A_PID" 2>/dev/null
  fi
  for _ in $(seq 1 40); do
    [ -z "$(pgrep -x ep-executord)" ] && break
    sleep 0.25
  done
  local inst
  inst=$(python3 -c "import json;print(json.load(open('$A_DIR/state/state.json'))['instance_id'])" 2>/dev/null)
  if [ -n "${inst:-}" ] && [ -d "$PIN_ROOT/$inst" ] &&
     [ "$(cat /sys/kernel/sched_ext/state 2>/dev/null)" = "disabled" ]; then
    rm -rf "$PIN_ROOT/$inst"
  fi
  rmdir "$PIN_ROOT" 2>/dev/null
  echo "FINAL state=$(cat /sys/kernel/sched_ext/state 2>/dev/null) ops=$(cat /sys/kernel/sched_ext/root/ops 2>/dev/null) v2_pins=$(ls -1 "$PIN_ROOT" 2>/dev/null | wc -l) seq=$(cat /sys/kernel/sched_ext/enable_seq 2>/dev/null)"
  echo "FINAL executors=$(pgrep -cx ep-executord 2>/dev/null || echo 0) loaders=$(pgrep -cx ep-sched-loader 2>/dev/null || echo 0) ref_loader=$(pgrep -cx scx_schedpilot 2>/dev/null || echo 0)"
}
trap cleanup EXIT

[ "$(id -u)" = 0 ] || { echo "requires root"; exit 77; }
[ -x "$REF" ] || { echo "reference loader missing: $REF"; exit 77; }
[ -x "$B/ep-executord" ] || { echo "build missing"; exit 77; }
[ "$(cat /sys/kernel/sched_ext/state)" = "disabled" ] || { echo "sched_ext not idle"; exit 1; }
[ -z "$(pgrep -x ep-executord)" ] || { echo "an executor already runs"; exit 1; }
[ -z "$(pgrep -x schedpilotd)" ] || { echo "schedpilotd is running; refusing"; exit 1; }

echo "PRE state=$(cat /sys/kernel/sched_ext/state) ops=$(cat /sys/kernel/sched_ext/root/ops 2>/dev/null) seq=$(cat /sys/kernel/sched_ext/enable_seq)"
echo "PRE ref_sha256=$(sha256sum "$REF" | cut -c1-16) v2_loader_sha256=$(sha256sum "$B/ep-sched-loader" | cut -c1-16)"
REF_PIN_BEFORE=$(ls -1 "$REF_PIN_ROOT" 2>/dev/null | wc -l)
echo "PRE ref_pin_entries=$REF_PIN_BEFORE"

rm -rf "$A_DIR"
mkdir -p "$A_DIR/state"
python3 - "$POLICY" <<'PY'
import json, sys
json.dump({
  "apiVersion": "eulerpilot.io/v1alpha1", "kind": "SchedulingPolicy",
  "metadata": {"name": "takeover-third-party"},
  "spec": {
    "scope": "scheduler-instance", "mode": "class", "expert": "latency-balanced",
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

"$B/ep-executord" --state-dir "$A_DIR/state" --socket "$A_DIR/a.sock" \
  --skill sched --loader "$B/ep-sched-loader" --sched-ctl "$B/ep-schedctl" \
  >"$A_DIR/daemon.log" 2>&1 &
A_PID=$!
for _ in $(seq 1 100); do "$B/ep" status --socket "$A_DIR/a.sock" >/dev/null 2>&1 && break; sleep 0.1; done

"$B/ep" apply --file "$POLICY" --idempotency-key take3-a-0001 --socket "$A_DIR/a.sock" \
  >"$A_DIR/apply.json" 2>&1
python3 -c "
import json
d = json.load(open('$A_DIR/apply.json'))
print('A_apply:', d['status'], d.get('error') or '')
assert d['status'] == 'VERIFIED', 'A did not attach; aborting probe'
" || { echo "PROBE_ABORTED: A apply failed"; exit 1; }
A_INST=$(python3 -c "import json;print(json.load(open('$A_DIR/state/state.json'))['instance_id'])")
echo "A instance=$A_INST seq_after_A=$(cat /sys/kernel/sched_ext/enable_seq) state=$(cat /sys/kernel/sched_ext/state) ops=$(cat /sys/kernel/sched_ext/root/ops 2>/dev/null)"
stat -c 'A_pin_inode=%i %n' "$PIN_ROOT/$A_INST/cfg" 2>/dev/null

echo "--- attaching third-party scheduler (ops=schedpilot) ---"
"$REF" --mode basic >"$A_DIR/ref.log" 2>&1 &
REF_PID=$!
sleep 1
echo "THIRD state=$(cat /sys/kernel/sched_ext/state) ops=$(cat /sys/kernel/sched_ext/root/ops 2>/dev/null) seq=$(cat /sys/kernel/sched_ext/enable_seq)"

echo "--- waiting 6s for A's liveness to react ---"
sleep 6
"$B/ep" status --socket "$A_DIR/a.sock" >"$A_DIR/status-after.json" 2>&1
python3 - "$A_DIR/status-after.json" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
fb = str(d.get("last_fallback", ""))
print("D1 A_degraded=", d["degraded"], "generation=", d["generation"])
print("D2 A_last_fallback=", fb[:220])
for label, want in (("ops based", "ops does not contain eulerpilot"),
                    ("enable_seq based", "enable_seq moved")):
    print("   reason is %s: %s" % (label, want in fb))
PY

echo "D3 after A rollback: state=$(cat /sys/kernel/sched_ext/state) ops=$(cat /sys/kernel/sched_ext/root/ops 2>/dev/null) third_party_still_in_place=$(cat /sys/kernel/sched_ext/state)"
stat -c 'A_pin_inode_after=%i %n' "$PIN_ROOT/$A_INST/cfg" 2>/dev/null || echo "A_pin_inode_after removed"
echo "D4 v2_pin_entries=$(ls -1 "$PIN_ROOT" 2>/dev/null | wc -l) ref_pin_entries=$(ls -1 "$REF_PIN_ROOT" 2>/dev/null | wc -l) (was $REF_PIN_BEFORE)"
echo "A daemon log:"
grep -E "liveness|auto fallback|RECOVERY" "$A_DIR/daemon.log" | tail -8 | sed 's/^/  /'

echo "--- stopping third-party loader ---"
kill -TERM "$REF_PID" 2>/dev/null
for _ in $(seq 1 40); do
  [ "$(cat /sys/kernel/sched_ext/state 2>/dev/null)" = "disabled" ] && break
  sleep 0.25
done
wait "$REF_PID" 2>/dev/null
REF_PID=""
echo "D5 after ref stop: state=$(cat /sys/kernel/sched_ext/state) ops=$(cat /sys/kernel/sched_ext/root/ops 2>/dev/null) v2_pins=$(ls -1 "$PIN_ROOT" 2>/dev/null | wc -l)"
