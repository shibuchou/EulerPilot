#!/usr/bin/env bash
# EulerPilot v2 -- real-kernel same-named takeover probe (SP3, root).
# Recorded 2026-10-22; results and caveats: takeover-20261022.txt.
# Outcome: the takeover never happens. Instance B is refused before attach,
# because recover_orphan() finds eulerpilot registered with no runtime marker
# of its own, so v2 cannot take over from itself. Requires /root/epv2-build
# binaries and an idle sched_ext.
# Real-kernel same-named takeover experiment.
#
# A runs the production path (sched: loader passes the struct_ops link FD over
# SCM_RIGHTS and exits, executor keeps holding it). B then attaches a second v2
# instance, which makes the kernel run B's scheduler: state stays "enabled" and
# root/ops keeps naming "eulerpilot", so the only signal A has left is that
# scx_enable_seq moved. We assert A notices and falls back, and that A's
# rollback does not tear B down.
set -uo pipefail

B=/root/epv2-build
A_DIR=/tmp/ep-take-A
B_DIR=/tmp/ep-take-B
POLICY=/tmp/ep-take-policy.json
PIN_ROOT=/sys/fs/bpf/eulerpilot-v2
A_PID=""
B_PID=""

fail() { echo "[FAIL] $*"; }

cleanup() {
  for pid in "$A_PID" "$B_PID"; do
    [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null && kill -TERM "$pid" 2>/dev/null
  done
  for _ in $(seq 1 60); do
    [ "$(cat /sys/kernel/sched_ext/state 2>/dev/null)" = "disabled" ] && break
    sleep 0.5
  done
  for d in "$A_DIR" "$B_DIR"; do
    inst=$(python3 -c "import json;print(json.load(open('$d/state/state.json'))['instance_id'])" 2>/dev/null)
    if [ -n "${inst:-}" ] && [ "$(cat /sys/kernel/sched_ext/state 2>/dev/null)" = "disabled" ]; then
      rm -rf "$PIN_ROOT/$inst"
    fi
  done
  rmdir "$PIN_ROOT" 2>/dev/null
  echo "final_state=$(cat /sys/kernel/sched_ext/state 2>/dev/null) pins=$(ls -1 $PIN_ROOT 2>/dev/null | wc -l)"
  echo "final_executors=$(pgrep -cx ep-executord || echo 0) loaders=$(pgrep -cx ep-sched-loader || echo 0)"
}
trap cleanup EXIT

[ "$(id -u)" = 0 ] || { fail "requires root"; exit 77; }
[ "$(cat /sys/kernel/sched_ext/state)" = "disabled" ] || { fail "sched_ext not idle"; exit 1; }
[ -z "$(pgrep -x ep-executord)" ] || { fail "an executor is already running"; exit 1; }
[ -z "$(pgrep -x schedpilotd)" ] || { fail "schedpilotd is running"; exit 1; }
echo "pre_state=$(cat /sys/kernel/sched_ext/state) pre_seq=$(cat /sys/kernel/sched_ext/enable_seq)"

rm -rf "$A_DIR" "$B_DIR"
mkdir -p "$A_DIR/state" "$B_DIR/state"

python3 - "$POLICY" <<'PY'
import json, sys
json.dump({
  "apiVersion": "eulerpilot.io/v1alpha1",
  "kind": "SchedulingPolicy",
  "metadata": {"name": "takeover-probe"},
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

"$B/ep" apply --file "$POLICY" --idempotency-key take-a-0001 --socket "$A_DIR/a.sock" \
  >"$A_DIR/apply.json" 2>&1
python3 -c "import json;d=json.load(open('$A_DIR/apply.json'));print('A_apply:',d['status'],d.get('error',''))"
A_INST=$(python3 -c "import json;print(json.load(open('$A_DIR/state/state.json'))['instance_id'])")
A_SEQ=$(cat /sys/kernel/sched_ext/enable_seq)
echo "A_instance=$A_INST seq_after_A=$A_SEQ ops=$(cat /sys/kernel/sched_ext/root/ops 2>/dev/null)"
ls "$PIN_ROOT" >"$A_DIR/pins-before-b.txt"

"$B/ep-executord" --state-dir "$B_DIR/state" --socket "$B_DIR/b.sock" \
  --skill sched-ref --loader "$B/ep-sched-loader" --sched-ctl "$B/ep-schedctl" \
  >"$B_DIR/daemon.log" 2>&1 &
B_PID=$!
for _ in $(seq 1 100); do "$B/ep" status --socket "$B_DIR/b.sock" >/dev/null 2>&1 && break; sleep 0.1; done

"$B/ep" apply --file "$POLICY" --idempotency-key take-b-0001 --socket "$B_DIR/b.sock" \
  >"$B_DIR/apply.json" 2>&1
python3 -c "import json;d=json.load(open('$B_DIR/apply.json'));print('B_apply:',d['status'],d.get('error',''))"
B_INST=$(python3 -c "import json;print(json.load(open('$B_DIR/state/state.json'))['instance_id'])")
echo "B_instance=$B_INST seq_after_B=$(cat /sys/kernel/sched_ext/enable_seq) ops=$(cat /sys/kernel/sched_ext/root/ops 2>/dev/null) state=$(cat /sys/kernel/sched_ext/state)"
stat -c '%i %n' "$PIN_ROOT/$B_INST" "$PIN_ROOT/$B_INST/cfg" 2>/dev/null | sed 's/^/B_pin: /'

echo "waiting 6s for A to notice the takeover..."
sleep 6
"$B/ep" status --socket "$A_DIR/a.sock" >"$A_DIR/status-after.json" 2>&1
python3 - "$A_DIR/status-after.json" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
fb = str(d.get("last_fallback", ""))
print("A_status:", d["status"], "degraded=", d["degraded"], "generation=", d["generation"])
print("A_last_fallback:", fb[:180])
checks = {
    "A noticed within ~3s": d["degraded"] is True,
    "reason names the lost scheduler": "scheduler lost" in fb,
    "reason cites enable_seq moving": "enable_seq moved" in fb,
}
for k, v in checks.items():
    print(("  PASS " if v else "  FAIL ") + k)
PY
echo "post_state=$(cat /sys/kernel/sched_ext/state) ops=$(cat /sys/kernel/sched_ext/root/ops 2>/dev/null)"
stat -c '%i %n' "$PIN_ROOT/$B_INST" "$PIN_ROOT/$B_INST/cfg" 2>/dev/null | sed 's/^/B_pin_after: /'
echo "A log tail:"
grep -E "liveness|auto fallback|RECOVERY" "$A_DIR/daemon.log" | tail -6 | sed 's/^/  /'
echo "B still healthy: state=$(cat /sys/kernel/sched_ext/state) b_pins=$(ls -1 "$PIN_ROOT/$B_INST" 2>/dev/null | wc -l)"
RB_ARGS=$(python3 -c "
import json
d = json.load(open('$B_DIR/apply.json'))
print('--of %s --transaction %s --snapshot %s --policy-sha256 %s' % (
    d['action_id'], d['transaction_id'], d['snapshot_id'], d['policy_sha256']))")
"$B/ep" rollback $RB_ARGS --socket "$B_DIR/b.sock" --idempotency-key take-b-rb01 \
  >"$B_DIR/rollback.json" 2>&1
python3 -c "import json;d=json.load(open('$B_DIR/rollback.json'));print('B_rollback:',d['status'],d.get('error',''))" || true
echo "after B rollback: state=$(cat /sys/kernel/sched_ext/state) pins=$(ls -1 $PIN_ROOT 2>/dev/null | wc -l)"
