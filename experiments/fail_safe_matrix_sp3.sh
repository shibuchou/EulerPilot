#!/usr/bin/env bash
# P1 fail-safe matrix -- fault-recovery layer on a real kernel (openEuler 24.03-LTS-SP3).
#
# Protocol §8 scenarios against the modes v2 can actually run (basic/class; adaptive is
# refused). Each scenario asserts behaviour after the fault: default fair back in charge,
# targets still making progress, and no effect left behind that the policy no longer
# claims. It measures no performance and asserts no percentage.
#
# usage: fail_safe_matrix_sp3.sh <ep-executord> <ep> <ep-sched-loader> <out-dir>
set -euo pipefail

EXECUTORD="${1:?usage: fail_safe_matrix_sp3.sh <ep-executord> <ep> <loader> <out-dir>}"
EP="${2:?}"
LOADER="${3:?}"
OUT="${4:?}"
CTL="$(dirname "$LOADER")/ep-schedctl"
PIN_ROOT=/sys/fs/bpf/eulerpilot-v2
CG_ROOT=/sys/fs/cgroup/fault-matrix
LOCK=/run/lock/eulerpilot-v2-fault.lock

skip() { echo "[skip] $*"; exit 77; }
[ "$(id -u)" = "0" ] || skip "requires root"
[ -x "$LOADER" ] || skip "loader not executable"
[ -x "$CTL" ] || skip "ep-schedctl not executable"
[ -d /sys/kernel/sched_ext ] || skip "sched_ext unavailable"
[ "$(cat /sys/kernel/sched_ext/state 2>/dev/null)" = "disabled" ] || skip "sched_ext is not idle"
if [ -d "$PIN_ROOT" ] && [ -n "$(ls -A "$PIN_ROOT" 2>/dev/null)" ]; then
  skip "pin root is not empty; a foreign instance is present"
fi

mkdir -p "$OUT"
WORK="$(mktemp -d /tmp/ep-fault.XXXXXX)"
RESULTS="$OUT/results.tsv"
: > "$RESULTS"
STATE="" ; SOCK="" ; DAEMON_PID="" ; PRODUCER_PID="" ; SERVICE_PID=""
MOUNTED="" ; OFFLINED="" ; LOG="$OUT/_pre.log"
REPLY_JSON="" ; ROLLBACK_JSON="" ; GEN_JSON=""

cleanup() {
  local rc=$?
  for pid in "$SERVICE_PID" "$PRODUCER_PID" "$DAEMON_PID"; do
    [ -n "$pid" ] && kill -TERM "$pid" 2>/dev/null || true
  done
  for pid in "$SERVICE_PID" "$PRODUCER_PID" "$DAEMON_PID"; do
    [ -n "$pid" ] && wait "$pid" 2>/dev/null || true
  done
  if [ -n "$MOUNTED" ]; then
    umount "$MOUNTED" 2>/dev/null || { sleep 1; umount "$MOUNTED" 2>/dev/null ||
      umount -l "$MOUNTED" 2>/dev/null || true; }
    rm -rf "$MOUNTED" 2>/dev/null || true
  fi
  if [ -n "$OFFLINED" ] && [ -w "/sys/devices/system/cpu/$OFFLINED/online" ]; then
    printf 1 > "/sys/devices/system/cpu/$OFFLINED/online" 2>/dev/null || true
  fi
  for pid in "$(cat "$WORK/svc.pid" 2>/dev/null || true)" \
             "$(cat "$WORK/bg.pid" 2>/dev/null || true)"; do
    [ -n "$pid" ] && printf %s "$pid" > /sys/fs/cgroup/cgroup.procs 2>/dev/null || true
  done
  rmdir "$CG_ROOT/svc" "$CG_ROOT/bg" "$CG_ROOT/orphan" "$CG_ROOT" 2>/dev/null || true
  if [ "$rc" -ne 0 ]; then
    echo "[FAIL] work directory retained: $WORK" >&2
  else
    rm -rf "$WORK"
  fi
}
trap cleanup EXIT

exec 9>"$LOCK"
flock -n 9 || skip "another fault matrix run holds $LOCK"

say() { printf '%s %s\n' "$(date -Is)" "$*" | tee -a "$LOG" >/dev/null; echo "    $*"; }
field() {
  python3 -c 'import json,sys
try:
    print(json.loads(sys.argv[1]).get(sys.argv[2], ""))
except Exception:
    print("")' "$1" "$2"
}
nested() {
  python3 -c 'import json,sys
try:
    d = json.loads(sys.argv[1])
except Exception:
    print("")
    raise SystemExit
print((d.get(sys.argv[2]) or {}).get(sys.argv[3], ""))' "$1" "$2" "$3"
}
poll_cmd() {
  local limit="$1"; shift
  local waited=0
  while ! "$@"; do
    sleep 0.25
    waited=$((waited + 250))
    [ "$waited" -ge "$limit" ] && return 1
  done
  return 0
}
state_is() { [ "$(cat /sys/kernel/sched_ext/state 2>/dev/null)" = "$1" ]; }
pins_empty() { [ ! -d "$PIN_ROOT" ] || [ -z "$(ls -A "$PIN_ROOT" 2>/dev/null)" ]; }
pin_count() {
  if [ -d "$PIN_ROOT" ]; then ls -A "$PIN_ROOT" 2>/dev/null | wc -l; else echo 0; fi
}
idle_now() { state_is disabled && pins_empty; }
daemon_log() { cat "$STATE/daemon.log" 2>/dev/null || true; }
journal_text() { cat "$STATE/journal.jsonl" 2>/dev/null || true; }

start_daemon() { # <skill> <state-dir> [extra executord args...]
  local skill="$1" state="$2"; shift 2
  STATE="$state"; SOCK="$STATE/executor.sock"
  mkdir -p "$STATE"
  "$EXECUTORD" --state-dir "$STATE" --socket "$SOCK" --skill "$skill" \
    --loader "$LOADER" --sched-ctl "$CTL" "$@" >>"$STATE/daemon.log" 2>&1 9>&- &
  DAEMON_PID=$!
  if ! poll_cmd 8000 "$EP" status --socket "$SOCK"; then
    say "executord did not become ready"
    daemon_log
    return 1
  fi
  return 0
}

stop_daemon() {
  [ -n "$DAEMON_PID" ] || return 0
  kill -TERM "$DAEMON_PID" 2>/dev/null || true
  wait "$DAEMON_PID" 2>/dev/null || true
  DAEMON_PID=""
  return 0
}

instance_id() {
  python3 -c 'import json,sys
try:
    print(json.load(open(sys.argv[1])).get("instance_id", ""))
except Exception:
    print("")' "$STATE/state.json"
}

policy_json() { # <out> <mode> <expert> <name> <policy_ttl_ms> <classes:yes|no>
  python3 - "$1" "$2" "$3" "$4" "$5" "$6" "$CG_ROOT/svc" "$CG_ROOT/bg" <<'PY'
import json, sys
out, mode, expert, name, ttl, classes, svc, bg = sys.argv[1:9]
targets = []
if classes == "yes":
    targets.append({"kind": "cgroup", "path": svc, "role": "FOREGROUND",
                    "role_source": "explicit", "class": "LAT"})
else:
    targets.append({"kind": "cgroup", "path": svc, "role": "FOREGROUND",
                    "role_source": "explicit"})
targets.append({"kind": "cgroup", "path": bg, "role": "BACKGROUND", "role_source": "explicit"})
policy = {
    "apiVersion": "eulerpilot.io/v1alpha1",
    "kind": "SchedulingPolicy",
    "metadata": {"name": name},
    "spec": {
        "scope": "scheduler-instance",
        "mode": mode,
        "expert": expert,
        "source": "human",
        "targets": targets,
        "parameters": {
            "lat_slice_ns": 2000000, "comp_slice_ns": 4000000,
            "cache_slice_ns": 4000000, "bg_slice_ns": 2000000,
            "preempt_threshold_ns": 500000, "bg_vtime_pct": 200,
            "llc_affinity": False,
        },
        "safety": {
            "min_dwell_ms": 5000, "max_observation_age_ms": 1000,
            "heartbeat_period_ms": 250, "heartbeat_timeout_ms": 2000,
            "policy_ttl_ms": int(ttl), "quiet_max_regression_pct": 5,
            "evaluation_min_bg_progress_ratio": 0.5, "bg_work_unit": "units",
            "bg_progress_window_ms": 5000, "min_bg_work_units_per_sec": 1,
            "max_bg_wait_ms": 1000, "fallback": "verified-safe-profile",
            "allow_affinity_change": False,
        },
    },
}
json.dump(policy, open(out, "w"))
PY
  return 0
}

minimal_policy() { # <out> <name> <mutation> -- one FOREGROUND target, no parameters
  python3 - "$1" "$2" "$3" "$CG_ROOT/svc" <<'PY'
import json, sys
out, name, mutation, svc = sys.argv[1:5]
policy = {
    "apiVersion": "eulerpilot.io/v1alpha1",
    "kind": "SchedulingPolicy",
    "metadata": {"name": name},
    "spec": {
        "scope": "scheduler-instance",
        "mode": "class",
        "expert": "latency-balanced",
        "source": "human",
        "targets": [{"kind": "cgroup", "path": svc, "role": "FOREGROUND",
                     "role_source": "explicit"}],
        "safety": {
            "min_dwell_ms": 5000, "max_observation_age_ms": 1000,
            "heartbeat_period_ms": 250, "heartbeat_timeout_ms": 2000,
            "policy_ttl_ms": 60000, "quiet_max_regression_pct": 5,
            "evaluation_min_bg_progress_ratio": 0.5, "bg_work_unit": "units",
            "bg_progress_window_ms": 5000, "min_bg_work_units_per_sec": 1,
            "max_bg_wait_ms": 1000, "fallback": "verified-safe-profile",
            "allow_affinity_change": False,
        },
    },
}
key, _, value = mutation.partition("=")
if key.startswith("targets."):
    node = policy["spec"]["targets"][0]
    field = key.split(".", 1)[1]
elif key.startswith("safety."):
    node = policy["spec"]["safety"]
    field = key.split(".", 1)[1]
else:
    node = policy["spec"]
    field = key
try:
    parsed = json.loads(value)
except Exception:
    parsed = value
node[field] = parsed
json.dump(policy, open(out, "w"))
PY
  return 0
}

start_workers() { # one foreground busy loop + one counting background producer
  mkdir -p "$CG_ROOT/svc" "$CG_ROOT/bg" "$CG_ROOT/orphan"
  bash -c 'while :; do :; done' 9>&- &
  SERVICE_PID=$!
  printf %s "$SERVICE_PID" > "$CG_ROOT/svc/cgroup.procs"
  printf %s "$SERVICE_PID" > "$WORK/svc.pid"
  : > "$WORK/bg.counter"
  bash -c 'n=0; while :; do n=$((n+1)); printf %s "$n" > "$1"; sleep 0.05; done' \
    bg-loop "$WORK/bg.counter" 9>&- &
  PRODUCER_PID=$!
  printf %s "$PRODUCER_PID" > "$CG_ROOT/bg/cgroup.procs"
  printf %s "$PRODUCER_PID" > "$WORK/bg.pid"
  return 0
}

bg_units() { cat "$WORK/bg.counter" 2>/dev/null || echo 0; }
workers_alive() {
  kill -0 "$SERVICE_PID" 2>/dev/null && kill -0 "$PRODUCER_PID" 2>/dev/null
}
workers_progress() { # both workers still consuming over 2s: fair is really in charge
  local before after u0 u1
  before="$(bg_units)"
  u0="$(awk '{print $14}' "/proc/$SERVICE_PID/stat")"
  sleep 2
  u1="$(awk '{print $14}' "/proc/$SERVICE_PID/stat")"
  after="$(bg_units)"
  [ "$after" -gt "$before" ] && [ "$u1" -gt "$u0" ]
}

apply_policy() { # <file> <idempotency-key> [ttl-ms] -> REPLY_JSON
  local file="$1" key="$2" ttl="${3:-}"
  local args=("$EP" apply --file "$file" --idempotency-key "$key" --socket "$SOCK")
  if [ -n "$ttl" ]; then args+=(--ttl-ms "$ttl"); fi
  REPLY_JSON="$("${args[@]}" 2>&1)" || true
  printf '%s\n' "$REPLY_JSON" >> "$LOG"
  return 0
}

rollback_apply() { # <reply> <idempotency-key> -> ROLLBACK_JSON
  local reply="$1" key="$2"
  ROLLBACK_JSON="$("$EP" rollback --of "$(field "$reply" action_id)" \
    --transaction "$(field "$reply" transaction_id)" \
    --snapshot "$(field "$reply" snapshot_id)" \
    --policy-sha256 "$(field "$reply" policy_sha256)" \
    --idempotency-key "$key" --socket "$SOCK" 2>&1)" || true
  printf '%s\n' "$ROLLBACK_JSON" >> "$LOG"
  return 0
}

pin_dir() { printf '%s/%s\n' "$PIN_ROOT" "$(instance_id)"; }

# class_of reads the kernel map, not our own bookkeeping.
class_of() { # <pid>
  local pid="$1" pin
  pin="$(pin_dir)"
  if [ ! -d "$pin" ]; then echo "no-pin-dir"; return 0; fi
  "$CTL" class-get --pin-dir "$pin" --pid "$pid" 2>/dev/null |
    python3 -c 'import json,sys
try:
    d = json.load(sys.stdin)
except Exception:
    print("helper-failed"); raise SystemExit
print(d.get("class") if d.get("present") else "absent")'
  return 0
}
class_absent() { # <pin> <pid>
  "$CTL" class-get --pin-dir "$1" --pid "$2" 2>/dev/null |
    python3 -c 'import json,sys
try:
    d = json.load(sys.stdin)
except Exception:
    raise SystemExit(1)
raise SystemExit(0 if not d.get("present") else 1)'
}
class_is() { # <pin> <pid> <class>
  "$CTL" class-get --pin-dir "$1" --pid "$2" 2>/dev/null |
    python3 -c 'import json,sys
try:
    d = json.load(sys.stdin)
except Exception:
    raise SystemExit(1)
raise SystemExit(0 if d.get("present") and d.get("class") == sys.argv[2] else 1)' "$2" "$3"
}

cfg_field() { # <pin> <key>
  local pin="$1" key="$2"
  if [ ! -d "$pin" ]; then echo "no-pin-dir"; return 0; fi
  "$CTL" get --pin-dir "$pin" 2>/dev/null |
    python3 -c 'import json,sys
try:
    print(json.load(sys.stdin)["cfg"].get(sys.argv[1], "?"))
except Exception:
    print("helper-failed")' "$key"
  return 0
}

our_pin_gone() { [ ! -d "$(pin_dir)" ]; }
dir_gone() { [ ! -d "$1" ]; }
no_new_pins() { [ "$(pin_count)" -le "$PIN_BASELINE" ]; }

record() { printf '%s\t%s\t%s\t%s\n' "$1" "$2" "$3" "$4" | tee -a "$RESULTS"; }
run_scenario() { # <id> <description> <function>
  local id="$1" desc="$2" fn="$3" rc=0
  LOG="$OUT/$id.log"; : > "$LOG"
  PIN_BASELINE="$(pin_count)"
  echo
  echo "=== $id: $desc ==="
  say "pin dirs at scenario start: $PIN_BASELINE"
  "$fn" || rc=$?
  stop_daemon || true
  if [ "$rc" -eq 77 ]; then
    record "$id" SKIP "$desc" "not exercisable on this machine, see $id.log"
    return 0
  fi
  if [ "$rc" -ne 0 ]; then
    record "$id" FAIL "$desc" "assertion failed (rc=$rc), see $id.log"
    daemon_log | tail -30 >> "$LOG"
    return 0
  fi
  # Every scenario is judged on its own instance: state back to disabled, and no pin dir
  # left behind *by this scenario*. A previous scenario's leak must not mask a verdict
  # here, and must not fabricate one either.
  if ! poll_cmd 20000 state_is disabled; then
    record "$id" FAIL "$desc" "sched_ext did not return to disabled"
    return 0
  fi
  if ! poll_cmd 10000 no_new_pins; then
    record "$id" FAIL "$desc" "this scenario left pin dirs behind ($PIN_BASELINE -> $(pin_count))"
    return 0
  fi
  record "$id" PASS "$desc" "assertions held, sched_ext disabled, no pins added by this scenario"
  return 0
}

# --------------------------------------------------------------------------
s01_class_attach_rollback() {
  start_daemon sched "$WORK/s01" || return 1
  policy_json "$WORK/s01.json" class latency-balanced fault-s01 60000 yes
  apply_policy "$WORK/s01.json" idem-fault-s01
  [ "$(field "$REPLY_JSON" status)" = VERIFIED ] || { say "apply not VERIFIED"; return 1; }
  poll_cmd 10000 state_is enabled || { say "never enabled"; return 1; }
  local svc_class bg_class
  svc_class="$(class_of "$SERVICE_PID")"
  bg_class="$(class_of "$PRODUCER_PID")"
  say "kernel class_map read-back while active: svc=$svc_class bg=$bg_class"
  [ "$svc_class" = LAT ] && [ "$bg_class" = BG ] || { say "declared classes not in map"; return 1; }
  rollback_apply "$REPLY_JSON" idem-fault-s01r
  say "rollback reply status=$(field "$ROLLBACK_JSON" status)"
  if grep -q "class revocation unconfirmed" <<<"$ROLLBACK_JSON$(daemon_log)"; then
    say "revocation was not confirmed by the map"; return 1
  fi
  if ! journal_text | grep -q "no class or cfg outlives the unpin"; then
    say "the post-rollback check did not report that nothing survives"; return 1
  fi
  if [ -d "$(pin_dir)" ]; then say "pin dir survived the rollback"; return 1; fi
  workers_alive || { say "workers died"; return 1; }
  return 0
}

s02_basic_attach_rollback() {
  start_daemon sched "$WORK/s02" || return 1
  policy_json "$WORK/s02.json" basic safe-basic fault-s02 60000 no
  apply_policy "$WORK/s02.json" idem-fault-s02
  [ "$(field "$REPLY_JSON" status)" = VERIFIED ] || { say "basic apply not VERIFIED"; return 1; }
  poll_cmd 10000 state_is enabled || return 1
  local bg_class
  bg_class="$(class_of "$PRODUCER_PID")"
  say "basic mode with an undeclared class: bg=$bg_class (BG is implied by the role)"
  rollback_apply "$REPLY_JSON" idem-fault-s02r
  poll_cmd 10000 state_is disabled || { say "basic rollback left it enabled"; return 1; }
  say "basic mode detached cleanly: status=$(field "$ROLLBACK_JSON" status)"
  return 0
}

s03_adaptive_refused() {
  start_daemon sched "$WORK/s03" || return 1
  policy_json "$WORK/s03.json" adaptive throughput-balanced fault-s03 60000 yes
  apply_policy "$WORK/s03.json" idem-fault-s03
  local status
  status="$(field "$REPLY_JSON" status)"
  say "adaptive apply status=$status detail=$(field "$REPLY_JSON" error)"
  [ "$status" = REJECTED ] || { say "adaptive was not refused"; return 1; }
  grep -qi "adaptive" <<<"$REPLY_JSON" || { say "refusal does not name adaptive"; return 1; }
  state_is disabled || { say "refusal had a side effect on sched_ext"; return 1; }
  no_new_pins || { say "refusal left pins"; return 1; }
  return 0
}

s04_kill_executor_attached() {
  start_daemon sched "$WORK/s04" || return 1
  policy_json "$WORK/s04.json" class latency-balanced fault-s04 60000 yes
  apply_policy "$WORK/s04.json" idem-fault-s04
  [ "$(field "$REPLY_JSON" status)" = VERIFIED ] || return 1
  poll_cmd 10000 state_is enabled || return 1
  local crashed_pin crashed_instance
  crashed_pin="$(pin_dir)"
  crashed_instance="$(instance_id)"
  [ -d "$crashed_pin" ] || { say "no pin dir of ours to track"; return 1; }
  local dead_pid=$DAEMON_PID
  DAEMON_PID=""
  kill -KILL "$dead_pid"
  say "SIGKILL sent to executor pid $dead_pid while a class policy was active"
  poll_cmd 15000 state_is disabled || { say "sched_ext stayed enabled after the crash"; return 1; }
  say "sched_ext back to disabled: the struct_ops link died with the executor"
  if [ -d "$crashed_pin" ]; then
    say "pinned maps of $crashed_instance are still in bpffs: a killed process unlinked nothing"
  else
    say "pinned maps already gone at crash time"
  fi
  workers_progress || { say "workers stopped progressing after fair took over"; return 1; }
  start_daemon sched "$WORK/s04" || return 1
  [ "$(instance_id)" = "$crashed_instance" ] || {
    say "the restart took a new instance id, so nobody owns the crashed instance's pins"
    return 1; }
  if ! poll_cmd 15000 dir_gone "$crashed_pin"; then
    say "DEFECT: the same instance came back and did not sweep its own leftover pins"
    return 1
  fi
  say "same-instance restart swept the pins left by the crash"
  "$EP" status --socket "$SOCK" > "$WORK/s04-status.json" 2>&1 || return 1
  python3 -c 'import json,sys
d = json.load(open(sys.argv[1]))
raise SystemExit(0 if not d.get("degraded", True) else 1)' "$WORK/s04-status.json" || {
    say "restart after the crash came back degraded"; return 1; }
  say "restart after SIGKILL: recovered without degradation"
  return 0
}

s05_kill_loader_schedref() {
  start_daemon sched-ref "$WORK/s05" || return 1
  policy_json "$WORK/s05.json" class latency-balanced fault-s05 60000 yes
  apply_policy "$WORK/s05.json" idem-fault-s05
  [ "$(field "$REPLY_JSON" status)" = VERIFIED ] || return 1
  poll_cmd 10000 state_is enabled || return 1
  local loader_pid
  loader_pid="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["pid"])' \
    "$WORK/s05/sched-runtime.json")"
  kill -KILL "$loader_pid"
  say "SIGKILL sent to the sched-ref loader pid $loader_pid"
  poll_cmd 15000 state_is disabled || {
    say "kernel did not detach when the link holder died"; return 1; }
  poll_cmd 10000 our_pin_gone || { say "pins not reclaimed after the loader died"; return 1; }
  if daemon_log | grep -q "auto fallback"; then
    say "executor reported: $(daemon_log | grep 'auto fallback' | head -1)"
  else
    say "note: no auto-fallback line yet (liveness needs 3 misses ~3s)"
    sleep 4
    daemon_log | grep -q "auto fallback" || { say "liveness never reacted"; return 1; }
    say "executor reported: $(daemon_log | grep 'auto fallback' | head -1)"
  fi
  workers_progress || { say "workers stalled after the data plane died"; return 1; }
  return 0
}

s06_lease_expiry() {
  start_daemon sched "$WORK/s06" || return 1
  policy_json "$WORK/s06.json" class latency-balanced fault-s06 60000 yes
  apply_policy "$WORK/s06.json" idem-fault-s06 4000
  [ "$(field "$REPLY_JSON" status)" = VERIFIED ] || return 1
  poll_cmd 10000 state_is enabled || return 1
  say "lease issued with ttl 4000ms and never renewed (the heartbeat-stop path)"
  poll_cmd 20000 state_is disabled || { say "lease expiry did not trigger a fallback"; return 1; }
  poll_cmd 10000 our_pin_gone || return 1
  daemon_log | grep -q "lease expired" || { say "fallback reason not recorded"; return 1; }
  say "fallback reason: $(daemon_log | grep 'auto fallback' | head -1)"
  workers_progress || return 1
  return 0
}

s07_policy_ttl_expiry() {
  start_daemon sched "$WORK/s07" || return 1
  # 10000ms is the schema floor for policy_ttl_ms; anything smaller is rejected before it
  # can be used as a timer, which is how this scenario first failed.
  policy_json "$WORK/s07.json" basic safe-basic fault-s07 10000 no
  apply_policy "$WORK/s07.json" idem-fault-s07 600000
  [ "$(field "$REPLY_JSON" status)" = VERIFIED ] || {
    say "apply failed: $(field "$REPLY_JSON" status) $(field "$REPLY_JSON" error)"; return 1; }
  poll_cmd 10000 state_is enabled || return 1
  say "policy TTL 10000ms with a long lease"
  poll_cmd 25000 state_is disabled || { say "policy TTL did not trigger a fallback"; return 1; }
  daemon_log | grep -q "policy TTL expired" || { say "TTL reason missing from the log"; return 1; }
  say "fallback reason: $(daemon_log | grep 'auto fallback' | head -1)"
  return 0
}

s08_invalid_policy() {
  start_daemon sched "$WORK/s08" || return 1
  local before after
  before="$("$EP" status --socket "$SOCK")"
  minimal_policy "$WORK/s08a.json" fault-s08a "unknown_field=1"
  minimal_policy "$WORK/s08b.json" fault-s08b "targets.class=SUPER"
  apply_policy "$WORK/s08a.json" idem-fault-s08a
  say "unknown field -> status=$(field "$REPLY_JSON" status)"
  [ "$(field "$REPLY_JSON" status)" = REJECTED ] || return 1
  apply_policy "$WORK/s08b.json" idem-fault-s08b
  say "bad class value -> status=$(field "$REPLY_JSON" status)"
  [ "$(field "$REPLY_JSON" status)" = REJECTED ] || return 1
  state_is disabled || { say "a rejected policy touched the kernel"; return 1; }
  no_new_pins || { say "a rejected policy left pins"; return 1; }
  after="$("$EP" status --socket "$SOCK")"
  [ "$(field "$before" generation)" = "$(field "$after" generation)" ] || {
    say "generation moved on a rejected apply"; return 1; }
  return 0
}

s09_out_of_range() {
  start_daemon sched "$WORK/s09" || return 1
  minimal_policy "$WORK/s09a.json" fault-s09a "safety.max_bg_wait_ms=20000"
  minimal_policy "$WORK/s09b.json" fault-s09b "safety.max_foreground_p99_ms=5000.0"
  apply_policy "$WORK/s09a.json" idem-fault-s09a
  say "max_bg_wait_ms=20000 -> status=$(field "$REPLY_JSON" status)"
  [ "$(field "$REPLY_JSON" status)" = REJECTED ] || return 1
  apply_policy "$WORK/s09b.json" idem-fault-s09b
  say "max_foreground_p99_ms=5000.0 (float for an integer field) -> status=$(field "$REPLY_JSON" status)"
  [ "$(field "$REPLY_JSON" status)" = REJECTED ] || return 1
  state_is disabled && no_new_pins || return 1
  return 0
}

s10_generation_conflict() {
  start_daemon sched "$WORK/s10" || return 1
  policy_json "$WORK/s10.json" class latency-balanced fault-s10 60000 yes
  apply_policy "$WORK/s10.json" idem-fault-s10
  [ "$(field "$REPLY_JSON" status)" = VERIFIED ] || return 1
  rollback_apply "$REPLY_JSON" idem-fault-s10r
  poll_cmd 10000 state_is disabled || return 1
  local sha
  sha="$(field "$REPLY_JSON" policy_sha256)"
  [ "${#sha}" = 64 ] || { say "no policy_sha256 to replay"; return 1; }
  GEN_JSON="$(python3 - "$SOCK" "$WORK/s10.json" "$sha" <<'PY'
import json, random, socket, sys, time
sock_path, policy_path, sha = sys.argv[1:4]

def iso(ms):
    base = int(ms)
    return time.strftime("%Y-%m-%dT%H:%M:%S", time.gmtime(base // 1000)) + \
        ".%03dZ" % (base % 1000)

def send(request):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(20)
    s.connect(sock_path)
    s.sendall((json.dumps(request) + "\n").encode())
    data = b""
    while b"\n" not in data:
        chunk = s.recv(65536)
        if not chunk:
            break
        data += chunk
    s.close()
    return json.loads(data.decode())

policy = json.load(open(policy_path))
lease = send({"operation": "lease.acquire", "request_id": "req-gen-conflict-1",
              "lease_owner": "fault-s10-owner", "lease_ttl_ms": 30000})
now = int(time.time() * 1000)
reply = send({
    "operation": "policy.apply",
    "policy": policy,
    "envelope": {
        "apiVersion": "eulerpilot.io/v1alpha1",
        "request_id": "req-%012x" % random.randrange(1 << 48),
        "transaction_id": "txn-%012x" % random.randrange(1 << 48),
        "idempotency_key": "idem-fault-s10x",
        "operation": "policy.apply",
        "policy_sha256": sha,
        "lease_owner": lease["lease_owner"],
        "fence_epoch": lease["fence_epoch"],
        "expected_generation": int(lease["observed_generation"]) + 7,
        "issued_at": iso(now),
        "deadline": iso(now + 60000),
    },
})
print(json.dumps({"status": reply.get("status"),
                  "code": (reply.get("error") or {}).get("code"),
                  "detail": (reply.get("error") or {}).get("detail")}))
PY
)"
  say "apply with expected_generation +7 -> $GEN_JSON"
  grep -q 'GENERATION_CONFLICT' <<<"$GEN_JSON" || { say "not a generation conflict"; return 1; }
  state_is disabled && no_new_pins || { say "the conflict had a side effect"; return 1; }
  return 0
}

s11_double_instance() {
  start_daemon sched "$WORK/s11" || return 1
  local second="$WORK/s11-second.log"
  if "$EXECUTORD" --state-dir "$WORK/s11" --socket "$WORK/s11-second.sock" \
       --skill sched --loader "$LOADER" --sched-ctl "$CTL" >"$second" 2>&1 9>&-; then
    say "a second executor on the same state dir started; that must not happen"
    return 1
  fi
  say "second instance refused: $(tr '\n' ' ' < "$second" | cut -c1-200)"
  grep -qi "another executor" "$second" || { say "refusal reason missing"; return 1; }
  state_is disabled || { say "the refused instance attached anyway"; return 1; }
  return 0
}

s12_external_value_change() {
  start_daemon sched "$WORK/s12" || return 1
  policy_json "$WORK/s12.json" class latency-balanced fault-s12 60000 yes
  apply_policy "$WORK/s12.json" idem-fault-s12 600000
  [ "$(field "$REPLY_JSON" status)" = VERIFIED ] || return 1
  poll_cmd 10000 state_is enabled || return 1
  local pin
  pin="$(pin_dir)"
  say "cfg before injection: lat_slice_ns=$(cfg_field "$pin" lat_slice_ns)"
  # 4000000 is inside the helper's own range (250000..10000000): an out-of-range injection
  # is refused by ep-schedctl itself, which tests the helper rather than the executor.
  "$CTL" set --pin-dir "$pin" --json '{"lat_slice_ns":4000000}' >> "$LOG" 2>&1 || {
    say "the out-of-band helper write itself failed"; return 1; }
  say "cfg after injection: lat_slice_ns=$(cfg_field "$pin" lat_slice_ns)"
  # Same actor reclassifies the BG task from outside, as a hand-run tool would.
  "$CTL" class-set --pin-dir "$pin" --pid "$PRODUCER_PID" --class COMP >> "$LOG" 2>&1 || true
  local before after
  before="$(class_of "$PRODUCER_PID")"
  if ! poll_cmd 12000 test_lat_slice_restored "$pin"; then
    say "cfg was NOT re-asserted within four refresh cycles"; return 1
  fi
  say "cfg re-asserted by the executor (drift check in refresh)"
  daemon_log | grep -q "cfg drift corrected" || {
    say "drift was corrected without a log line"; return 1; }
  after="$(class_of "$PRODUCER_PID")"
  say "external class change: before=$before after=$after"
  if [ "$after" = COMP ]; then
    say "LIMITATION: per-entry class drift is not re-asserted by refresh (recorded, not fixed)"
  fi
  local t0 t1
  t0="$(date +%s%N)"
  for _ in $(seq 1 20); do "$CTL" get --pin-dir "$pin" >/dev/null 2>&1 || true; done
  t1="$(date +%s%N)"
  say "cost of one map read-back used by the drift check: $(( (t1 - t0) / 20 / 1000000 ))ms"
  rollback_apply "$REPLY_JSON" idem-fault-s12r
  return 0
}
test_lat_slice_restored() { [ "$(cfg_field "$1" lat_slice_ns)" = 2000000 ]; }

s13_member_migrates_out() {
  start_daemon sched "$WORK/s13" || return 1
  policy_json "$WORK/s13.json" class latency-balanced fault-s13 60000 yes
  apply_policy "$WORK/s13.json" idem-fault-s13 600000
  [ "$(field "$REPLY_JSON" status)" = VERIFIED ] || return 1
  poll_cmd 10000 state_is enabled || return 1
  local pin
  pin="$(pin_dir)"
  class_is "$pin" "$PRODUCER_PID" BG || { say "bg worker not classified before the move"; return 1; }
  printf %s "$PRODUCER_PID" > "$CG_ROOT/orphan/cgroup.procs"
  say "moved the BG worker pid $PRODUCER_PID out of the declared cgroup"
  poll_cmd 15000 class_absent "$pin" "$PRODUCER_PID" || {
    say "class entry for the migrated-out task survived revocation"; return 1; }
  say "class entry revoked and read back as absent from the kernel map"
  printf %s "$PRODUCER_PID" > "$CG_ROOT/bg/cgroup.procs"
  poll_cmd 15000 class_is "$pin" "$PRODUCER_PID" BG || {
    say "re-entry into the cgroup did not re-classify"; return 1; }
  say "re-entry re-classified to BG"
  rollback_apply "$REPLY_JSON" idem-fault-s13r
  return 0
}

s14_journal_disk_full() {
  MOUNTED="$WORK/tiny"
  mkdir -p "$MOUNTED"
  mount -t tmpfs -o size=4m tmpfs "$MOUNTED" || { say "cannot mount a 4m tmpfs"; return 1; }
  start_daemon sched "$MOUNTED/exec" || return 1
  policy_json "$MOUNTED/s14.json" class latency-balanced fault-s14 60000 yes
  apply_policy "$MOUNTED/s14.json" idem-fault-s14 600000
  [ "$(field "$REPLY_JSON" status)" = VERIFIED ] || { say "baseline apply failed"; return 1; }
  poll_cmd 10000 state_is enabled || return 1
  local applied_reply="$REPLY_JSON"
  local n=0
  while dd if=/dev/zero of="$MOUNTED/filler$n" bs=4k count=1 2>/dev/null; do
    n=$((n + 1))
  done
  say "tmpfs state dir filled with $n filler files (ENOSPC from here on)"

  # 1) A *new* effect must still be refused: an audit trail we cannot write is not a
  #    transaction, so fail-closed wins here even though teardown must not be blocked.
  apply_policy "$MOUNTED/s14.json" idem-fault-s14b 600000
  local refuse_status
  refuse_status="$(field "$REPLY_JSON" status)"
  say "second apply under ENOSPC -> $refuse_status"
  [ "$refuse_status" = RECOVERY_REQUIRED ] || {
    say "a new effect was not refused while the store was unwritable"; return 1; }

  # 2) The unwritable store has to be visible from the read-only API, not only in stderr.
  local st
  st="$("$EP" status --socket "$SOCK")" || { say "status failed under ENOSPC"; return 1; }
  say "status: degraded=$(field "$st" degraded) store_warning='$(field "$st" store_warning)'"
  [ -n "$(field "$st" store_warning)" ] || { say "no store warning reported"; return 1; }

  # 3) Teardown must reach the skill even though neither state.json nor the journal is
  #    writable -- taking protection off is always the safe direction.
  rollback_apply "$applied_reply" idem-fault-s14r
  local rs
  rs="$(field "$ROLLBACK_JSON" status)"
  say "rollback under ENOSPC -> status=$rs"
  daemon_log | tail -20 >> "$LOG"
  poll_cmd 15000 state_is disabled || {
    say "DEFECT: rollback did not restore default fair while the store was unwritable"
    return 1; }
  poll_cmd 10000 our_pin_gone || { say "pins left behind while the journal was full"; return 1; }
  if grep -qE 'AUDIT_PENDING|EFFECT_RESTORED' <<<"$ROLLBACK_JSON"; then
    say "teardown completed and told the truth about the audit: $rs"
  elif [ "$rs" = ROLLED_BACK ]; then
    say "teardown reported a clean rollback"
  else
    say "unexpected rollback reply under ENOSPC: $rs"; return 1
  fi
  workers_progress || { say "targets stopped progressing after the forced teardown"; return 1; }
  # The daemon still holds the socket/lock/journal open on this tmpfs, so unmounting here
  # would fail with EBUSY and make the whole run look red. The EXIT trap unmounts it after
  # the daemon is reaped; the daemon log has already been copied into this scenario's log.
  say "tmpfs left mounted on purpose: it is unmounted by the exit trap after the daemon dies"
  return 0
}

reported_loss() { daemon_log | grep -qE "auto fallback|liveness miss"; }

s15_cpu_hotplug() {
  local cpu="" candidate f
  for candidate in 7 6 5 4; do
    f="/sys/devices/system/cpu/cpu${candidate}/online"
    if [ -f "$f" ] && [ "$(cat "$f")" = "1" ]; then cpu="cpu${candidate}"; break; fi
  done
  [ -n "$cpu" ] || { say "no offline-capable cpu on this machine"; return 77; }
  start_daemon sched "$WORK/s15" || return 1
  policy_json "$WORK/s15.json" class latency-balanced fault-s15 60000 yes
  apply_policy "$WORK/s15.json" idem-fault-s15 600000
  [ "$(field "$REPLY_JSON" status)" = VERIFIED ] || return 1
  poll_cmd 10000 state_is enabled || return 1
  local seq_before hot_before state_after
  seq_before="$(cat /sys/kernel/sched_ext/enable_seq)"
  hot_before="$(cat /sys/kernel/sched_ext/hotplug_seq 2>/dev/null || echo 0)"
  OFFLINED="$cpu"
  printf 0 > "/sys/devices/system/cpu/$cpu/online"
  say "offlined $cpu while the scheduler was attached"
  sleep 2
  state_after="$(cat /sys/kernel/sched_ext/state 2>/dev/null)"
  printf 1 > "/sys/devices/system/cpu/$cpu/online"
  OFFLINED=""
  sleep 2
  say "sched_ext state during the offline: $state_after"
  dmesg 2>/dev/null | grep -E "smpboot|sched_ext" | tail -4 >> "$LOG" || true
  workers_progress || { say "workers stopped progressing across the hotplug"; return 1; }
  if [ "$state_after" = enabled ]; then
    # This kernel tolerated the hotplug: the fail-safe question is whether we stayed quiet.
    [ "$(cat /sys/kernel/sched_ext/enable_seq)" = "$seq_before" ] || {
      say "hotplug moved enable_seq, which would false-trigger the presence check"; return 1; }
    daemon_log | grep -qE "auto fallback|liveness miss" && {
      say "a tolerated hotplug was misreported as a lost scheduler"; return 1; }
    say "kernel kept eulerpilot attached; the executor stayed silent (correct)"
    rollback_apply "$REPLY_JSON" idem-fault-s15r
    return 0
  fi
  say "this kernel unregisters the BPF scheduler on cpu hotplug (see the dmesg lines above);"
  say "  enable_seq $seq_before, hotplug_seq $hot_before -> $(cat /sys/kernel/sched_ext/hotplug_seq 2>/dev/null || echo 0)"
  say "  hotplug is therefore a scheduler-lifecycle event on this kernel, not a benign no-op"
  if ! poll_cmd 15000 reported_loss; then
    say "the executor never noticed that the kernel dropped its scheduler"; return 1
  fi
  say "executor detected the loss: $(daemon_log | grep -E 'auto fallback|liveness miss' | tail -1)"
  stop_daemon
  if ! poll_cmd 15000 our_pin_gone; then
    say "our pinned maps survived the shutdown after a hotplug loss"; return 1
  fi
  say "shutdown after the loss left no pins of ours"
  return 0
}

# Apply writes cfg first and class entries second. If the second step fails, the whole
# apply must be refused, the attach aborted, and nothing left in the kernel -- and the
# machine must accept a clean retry afterwards.
s17_second_step_failure() {
  local wrapper="$WORK/ctrl-refuses-class-set.sh"
  cat > "$wrapper" <<WRAPPER
#!/usr/bin/env bash
log="\${FAKE_CTL_LOG:-/dev/null}"
printf '%s\\n' "\$*" >> "\$log"
if [ "\$1" = "class-set" ]; then
  echo '{"ok":false,"error":"injected: this helper refuses class-set"}' >&2
  exit 1
fi
exec "\${REALCTL}" "\$@"
WRAPPER
  chmod 0755 "$wrapper"
  : > "$WORK/s17.calls"
  export FAKE_CTL_LOG="$WORK/s17.calls" REALCTL="$CTL"
  start_daemon sched "$WORK/s17" --sched-ctl "$wrapper" || { unset FAKE_CTL_LOG REALCTL; return 1; }
  policy_json "$WORK/s17.json" class latency-balanced fault-s17 60000 yes
  apply_policy "$WORK/s17.json" idem-fault-s17
  local status code gen
  status="$(field "$REPLY_JSON" status)"
  say "apply whose second step (class-set) failed -> status=$status"
  # The contract here is "never claim success, and take back what step one did".
  # Compensation of an aborted attach reports ROLLED_BACK with error.code APPLY_FAILED:
  # the loader was stopped and its pins unlinked, so the compensating rollback itself
  # found nothing attached. REJECTED would only be right if we refused before touching
  # the kernel, which is not this case.
  case "$status" in
    REJECTED|ROLLED_BACK|DEGRADED|RECOVERY_REQUIRED) ;;
    *) say "a failed second step returned $status"; return 1 ;;
  esac
  code="$(nested "$REPLY_JSON" error code)"
  say "error.code=$code"
  [ "$code" = APPLY_FAILED ] || { say "the failure was not reported as APPLY_FAILED"; return 1; }
  grep -qi "class-set" <<<"$REPLY_JSON" || {
    say "the rejection does not name the failing step"; return 1; }
  poll_cmd 15000 state_is disabled || { say "aborted attach left sched_ext enabled"; return 1; }
  poll_cmd 10000 pins_empty || { say "aborted attach left pins"; return 1; }
  gen="$(field "$("$EP" status --socket "$SOCK")" generation)"
  say "generation after the aborted apply: $gen (0 = nothing committed)"
  [ "$gen" = 0 ] || { say "the aborted apply advanced the generation"; return 1; }
  local calls
  calls="$(cat "$WORK/s17.calls")"
  printf 'helper calls seen:\n%s\n' "$calls" >> "$LOG"
  grep -q '^get --pin-dir' <<<"$calls" || { say "cfg snapshot step missing"; return 1; }
  grep -q '^set --pin-dir' <<<"$calls" || { say "cfg write step missing"; return 1; }
  grep -q '^class-set' <<<"$calls" || { say "the injected step never ran"; return 1; }
  say "step order recorded: get -> set -> class-set(refused)"
  workers_progress || { say "targets stopped progressing after the aborted apply"; return 1; }

  # Recovery: the same instance with the real helper must be able to apply cleanly.
  stop_daemon
  unset FAKE_CTL_LOG REALCTL
  start_daemon sched "$WORK/s17" || return 1
  apply_policy "$WORK/s17.json" idem-fault-s17b
  [ "$(field "$REPLY_JSON" status)" = VERIFIED ] || {
    say "retry after the failed second step did not verify"; return 1; }
  poll_cmd 10000 state_is enabled || return 1
  say "retry after the aborted apply verified normally"
  rollback_apply "$REPLY_JSON" idem-fault-s17c
  poll_cmd 15000 state_is disabled || return 1
  return 0
}

s16_not_exercisable() {
  say "PMU refusal: v2 has no PMU consumer (scripts/doctor.py reports pmu_read_test NOT_RUN),"
  say "  so there is no code path that could refuse it -- UNSUPPORTED, not a pass."
  say "heartbeat stop: heartbeat_ns is only consulted in adaptive mode, which v2 refuses at"
  say "  apply (S03), and nothing in v2 ever writes it -- UNSUPPORTED by design."
  say "PID re-creation: a pidfd is held for the transaction, so a recycled pid cannot be"
  say "  re-interpreted under it; task death is covered here and in tests/integration_fd.sh."
  say "TID targets: v2 binds tgids only (class_map is keyed by tgid) -- out of scope for P1."
  return 0
}

# --------------------------------------------------------------------------
echo "=== fail-safe matrix start $(date -Is) ==="
echo "kernel: $(uname -r)"
echo "executor: $EXECUTORD"
echo "out: $OUT"
sha256sum "$EXECUTORD" "$EP" "$LOADER" "$CTL" > "$OUT/SHA256SUMS"
sha256sum "$0" > "$OUT/driver-SHA256SUMS"

start_workers
run_scenario S01 "class 模式挂载+回退：内核 class 读回与撤销证明" s01_class_attach_rollback
run_scenario S02 "basic 模式挂载+回退干净" s02_basic_attach_rollback
run_scenario S03 "adaptive 模式在真实 RPC 上被拒（fail-closed）" s03_adaptive_refused
run_scenario S04 "挂载状态下 SIGKILL 执行服务：崩溃后 fair 接管与重启对账" s04_kill_executor_attached
run_scenario S05 "sched-ref 下 SIGKILL loader：存活检测与补偿回退" s05_kill_loader_schedref
run_scenario S06 "租约到期（心跳停止的等价路径）自动回退" s06_lease_expiry
run_scenario S07 "策略 TTL 到期自动回退（basic）" s07_policy_ttl_expiry
run_scenario S08 "无效策略在真实 RPC 上被拒且无副作用" s08_invalid_policy
run_scenario S09 "越界与错误类型的阈值被拒" s09_out_of_range
run_scenario S10 "generation 冲突被拒且无副作用" s10_generation_conflict
run_scenario S11 "第二实例拒绝" s11_double_instance
run_scenario S12 "外部改值：cfg 漂移检测与再断言" s12_external_value_change
run_scenario S13 "成员迁出：class 撤销并读回为不存在" s13_member_migrates_out
run_scenario S14 "日志盘满（tmpfs ENOSPC）下的回退可见性" s14_journal_disk_full
run_scenario S15 "CPU 热插拔不误判为调度器丢失" s15_cpu_hotplug
run_scenario S17 "两步写入的第二步失败：拒绝、中止挂载、之后可干净重试" s17_second_step_failure
run_scenario S16 "无法在 v2 中构造的场景如实标注" s16_not_exercisable

echo
echo "=== results ==="
column -t -s "$(printf '\t')" "$RESULTS" | tee "$OUT/results-column.txt"
echo
ls -A "$PIN_ROOT" 2>/dev/null > "$OUT/leftover-pin-dirs.txt" || true
if state_is disabled; then
  echo "[ok] sched_ext is disabled at the end of the matrix"
else
  echo "[FAIL] sched_ext is $(cat /sys/kernel/sched_ext/state) at the end"
  exit 1
fi
if pins_empty; then
  echo "[ok] no pin dirs left: state=$(cat /sys/kernel/sched_ext/state) pins=0"
else
  echo "[FAIL] pin dirs left in bpffs (pinned maps keep kernel memory until unlinked):"
  for dir in $(ls -A "$PIN_ROOT" 2>/dev/null); do
    owner="$(grep -rl "$dir" "$WORK" --include=state.json 2>/dev/null | head -1)"
    printf '    %s <- %s\n' "$dir" "${owner:-not created by this run}"
  done
  exit 1
fi
if grep -q "$(printf '\t')FAIL$(printf '\t')" "$RESULTS"; then
  echo "[FAIL] at least one scenario failed"
  exit 1
fi
echo "=== fail-safe matrix done $(date -Is) ==="
