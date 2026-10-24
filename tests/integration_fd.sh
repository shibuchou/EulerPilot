#!/usr/bin/env bash
# Real sched_ext integration test for FD-passing link ownership (--skill sched).
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
BG_PID=""
CGROUP=""

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
       "role": "FOREGROUND", "role_source": "explicit"},
      {"kind": "pid", "pid": 1, "role": "FOREGROUND", "role_source": "explicit"}
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
      "max_foreground_p99_ms": 50,
      "max_bg_wait_ms": 1000,
      "fallback": "verified-safe-profile",
      "allow_affinity_change": false
    }
  }
}
JSON

sleep 300 >/dev/null 2>&1 &
BG_PID=$!
CGROUP="/sys/fs/cgroup/ep-v2-test-$$"
mkdir -p "$CGROUP"
echo "$BG_PID" >"$CGROUP/cgroup.procs"
python3 - "$POLICY" "$CGROUP" <<'PY'
import json, sys
policy = json.load(open(sys.argv[1]))
policy["spec"]["targets"].append(
    {"kind": "cgroup", "path": sys.argv[2], "role": "BACKGROUND",
     "role_source": "explicit", "class": "BG"})
# A non-default class on a FOREGROUND pid target. This is the path a policy could
# not express before: LAT/CACHE existed in the data plane but nothing could feed them.
for target in policy["spec"]["targets"]:
    if target.get("kind") == "pid":
        target["class"] = "LAT"
    elif target.get("kind") == "cgroup" and target.get("role") == "FOREGROUND":
        # COMPUTE is the policy-spelling of the helper's COMP tier; the mapping lives in
        # ctrl_class_set, and this line is what exercises it.
        target["class"] = "COMPUTE"
json.dump(policy, open(sys.argv[1], "w"))
PY

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
  # Resource cleanup belongs to the owning executor/reconcile path.
  # Never delete another instance's pins or erase failed recovery evidence.
}

cleanup() {
  local result=$?
  if [ -n "$BG_PID" ] && kill -0 "$BG_PID" 2>/dev/null; then
    kill -TERM "$BG_PID" 2>/dev/null || true
    wait "$BG_PID" 2>/dev/null || true
  fi
  if [ -n "$CGROUP" ] && [ -d "$CGROUP" ]; then
    rmdir "$CGROUP" 2>/dev/null || true
  fi
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
  "$EXECUTORD" --state-dir "$STATE" --socket "$SOCK" --skill sched --loader "$LOADER" \
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

echo "[integration-fd] loader=$LOADER"

start_daemon
status="$("$EP" status --socket "$SOCK")" || fail "status failed"
[ "$(field "$status" status)" = "OK" ] || fail "initial status not OK: $status"
INSTANCE="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["instance_id"])' "$STATE/state.json")"
PINDIR="/sys/fs/bpf/eulerpilot-v2/$INSTANCE"
echo "[integration-fd] instance=$INSTANCE pin_dir=$PINDIR"

echo "[integration-fd] validate policy"
validation="$("$EP" validate --file "$POLICY" --socket "$SOCK")" || fail "policy validation failed"
[ "$(field "$validation" status)" = "OK" ] || fail "validate not OK: $validation"
[ "$(field "$validation" mode)" = "class" ] || fail "validated mode is not class"
[ "$(field "$validation" expert)" = "latency-balanced" ] || fail "validated expert mismatch"

echo "[integration-fd] non-human SLO veto"
python3 - "$POLICY" "$TMP/llm.json" <<'PY'
import json, sys
policy = json.load(open(sys.argv[1]))
policy["spec"]["source"] = "llm-proposal"
json.dump(policy, open(sys.argv[2], "w"))
PY
llm=""
if ! llm="$("$EP" apply --file "$TMP/llm.json" --idempotency-key idem-llm-0001 --socket "$SOCK")"; then
  :
fi
[ -n "$llm" ] || fail "non-human apply produced no response"
python3 - "$llm" <<'PY'
import json, sys
reply = json.loads(sys.argv[1])
assert reply.get("status") != "VERIFIED", reply
assert reply["error"]["code"] == "SLO_DENIED", reply
print("[integration-fd] non-human apply denied:", reply["error"]["code"])
PY
[ "$(cat /sys/kernel/sched_ext/state)" = "disabled" ] ||
  fail "SLO-denied apply changed scheduler state"

echo "[integration-fd] apply"
resp="$("$EP" apply --file "$POLICY" --idempotency-key idem-sched-int-0001 --socket "$SOCK")" ||
  fail "apply failed"
[ "$(field "$resp" status)" = "VERIFIED" ] || fail "apply not VERIFIED: $resp"
[ "$(field "$resp" observed_generation)" = "1" ] || fail "generation is not 1"
[ "$(cat /sys/kernel/sched_ext/state)" = "enabled" ] || fail "sched_ext not enabled after apply"
grep -q eulerpilot /sys/kernel/sched_ext/root/ops || fail "ops does not contain eulerpilot"
[ -f "$PINDIR"/cfg ] || fail "cfg pin missing"
[ -f "$STATE/sched-runtime.json" ] || fail "runtime marker missing"
if pgrep -x ep-sched-loader >/dev/null 2>&1; then
  fail "loader is still running after passing the link fd"
fi
if ! ls -l "/proc/$DAEMON_PID/fd" 2>/dev/null | grep -qi bpf; then
  fail "executor does not hold a bpf link fd"
fi
LINK_FD=""
for f in /proc/"$DAEMON_PID"/fd/*; do
  target="$(readlink "$f" 2>/dev/null || true)"
  case "$target" in
    *bpf*)
      LINK_FD="$(basename "$f")"
      break
      ;;
  esac
done
[ -n "$LINK_FD" ] || fail "cannot identify the executor link fd"
FLAGS="$(awk '/^flags:/{print $2}' "/proc/$DAEMON_PID/fdinfo/$LINK_FD")"
[ -n "$FLAGS" ] || fail "cannot read link fd flags"
[ $((0$FLAGS & 02000000)) -ne 0 ] || fail "executor link fd is not CLOEXEC"
echo "[integration-fd] executor holds a CLOEXEC bpf link fd; loader has exited"

echo "[integration-fd] cfg parameters"
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
assert cfg["lat_budget_pct"] == 60, cfg
assert cfg["budget_window_ms"] == 100, cfg
assert cfg["aging_ms"] == 20, cfg
assert cfg["llc_affinity"] is False, cfg
assert cfg["generation"] >= 2, cfg
print("[integration-fd] cfg generation", cfg["generation"])
PY

echo "[integration-fd] fairness cfg fields"
setcfg="$("$CTL" set --pin-dir "$PINDIR" --json '{"lat_budget_pct":50,"budget_window_ms":50,"aging_ms":10}')" ||
  fail "fairness cfg set failed"
python3 - "$setcfg" <<'PY'
import json, sys
reply = json.loads(sys.argv[1])
assert reply.get("ok") is True, reply
cfg = reply["cfg"]
assert cfg["lat_budget_pct"] == 50, cfg
assert cfg["budget_window_ms"] == 50, cfg
assert cfg["aging_ms"] == 10, cfg
print("[integration-fd] fairness cfg set", cfg["lat_budget_pct"], cfg["budget_window_ms"], cfg["aging_ms"])
PY
restorecfg="$("$CTL" set --pin-dir "$PINDIR" --json '{"lat_budget_pct":60,"budget_window_ms":100,"aging_ms":20}')" ||
  fail "fairness cfg restore failed"
python3 - "$restorecfg" <<'PY'
import json, sys
reply = json.loads(sys.argv[1])
assert reply.get("ok") is True, reply
cfg = reply["cfg"]
assert cfg["lat_budget_pct"] == 60, cfg
assert cfg["budget_window_ms"] == 100, cfg
assert cfg["aging_ms"] == 20, cfg
print("[integration-fd] fairness cfg restored")
PY

python3 - "$resp" <<'PY'
import json, sys
reply = json.loads(sys.argv[1])
bindings = reply.get("target_bindings") or []
assert bindings, reply
cgroup_id = bindings[0].get("cgroup_id", 0)
assert cgroup_id > 0, bindings
print("[integration-fd] cgroup_id", cgroup_id)
pids = [b for b in bindings if b.get("kind") == "pid"]
assert pids and pids[0].get("pid") == 1, bindings
assert pids[0].get("starttime", 0) > 0, bindings
print("[integration-fd] pid target", pids[0]["pid"], pids[0]["starttime"])
# What the policy declared, and what the skill therefore wrote: a FOREGROUND target
# can now carry LAT, which is the path that used to be unreachable from a policy.
declared = sorted({(b.get("class"), b.get("effective_class")) for b in bindings})
assert ("LAT", "LAT") in declared, bindings
assert ("COMPUTE", "COMPUTE") in declared, bindings
assert ("BG", "BG") in declared, bindings
print("[integration-fd] declared/effective classes:", declared)
PY

echo "[integration-fd] background class map"
lat_cls="$("$CTL" class-get --pin-dir "$PINDIR" --pid 1)" || fail "class-get (LAT) failed"
python3 - "$lat_cls" <<'PY'
import json, sys
reply = json.loads(sys.argv[1])
assert reply.get("present") is True, reply
assert reply.get("class") == "LAT", reply
print("[integration-fd] declared class read back from the kernel map:", reply["class"])
PY

cls="$("$CTL" class-get --pin-dir "$PINDIR" --pid "$BG_PID")" || fail "class-get failed"
python3 - "$cls" <<'PY'
import json, sys
reply = json.loads(sys.argv[1])
assert reply.get("present") is True, reply
assert reply.get("class") == "BG", reply
assert reply.get("source") == 2, reply
print("[integration-fd] background pid class", reply["class"])
PY

slo="$("$EP" slo --expert latency-balanced --socket "$SOCK")" || fail "slo failed"
python3 - "$slo" <<'PY'
import json, sys
reply = json.loads(sys.argv[1])
assert reply.get("status") == "OK", reply
assert reply.get("allowed") is False, reply
assert reply.get("code") == "FEATURES_INSUFFICIENT", reply
print("[integration-fd] slo denied:", reply["code"])
PY

echo "[integration-fd] service observation"
# The instance runs --skill sched (link FD over SCM_RIGHTS), so it must not
# describe itself as the child-process reference mode.
[ "$(field "$("$EP" status --socket "$SOCK")" skill)" = "sched" ] ||
  fail "status reports the wrong skill mode"
NOW_MS="$(python3 -c 'import time; print(int(time.time() * 1000))')"
write_obs() {  # name expert p99 units wait
  cat >"$TMP/$1.json" <<JSON
{"workload": "svc-integration", "observed_at_ms": $NOW_MS, "window_ms": 5000,
 "expert": "$2", "foreground": {"p99_latency_ms": $3},
 "background": {"work_units_per_sec": $4, "max_wait_ms": $5}}
JSON
}
write_obs obs-good latency-balanced 1.6 900 40
"$EP" observe --file "$TMP/obs-good.json" --socket "$SOCK" >"$TMP/obs1.json" 2>&1 ||
  fail "observe rejected a valid observation: $(cat "$TMP/obs1.json")"
python3 - "$TMP/obs1.json" <<'PY'
import json, sys
reply = json.load(open(sys.argv[1]))
assert reply.get("status") == "OK", reply
assert reply["accepted"]["workload"] == "svc-integration", reply
assert reply.get("switch_outcome") == {}, reply
print("[integration-fd] observation accepted for", reply["accepted"]["expert"])
PY

write_obs obs-ceiling latency-balanced 900.0 900 40
"$EP" observe --file "$TMP/obs-ceiling.json" --socket "$SOCK" >/dev/null 2>&1 ||
  fail "observe rejected the ceiling-breaching measurement"
slo="$("$EP" slo --expert latency-balanced --socket "$SOCK")" || fail "slo after ceiling breach failed"
python3 - "$slo" <<'CEIL'
import json, sys
reply = json.loads(sys.argv[1])
assert reply.get("allowed") is False, reply
# The absolute ceiling outranks the resolution guard: a single-window fact needs
# no repeat measurement.
assert reply.get("code") == "FOREGROUND_P99_EXCEEDED", reply
print("[integration-fd] foreground ceiling enforced:", reply["code"])
CEIL

write_obs obs-starving latency-balanced 1.6 0.01 40
"$EP" observe --file "$TMP/obs-starving.json" --socket "$SOCK" >/dev/null 2>&1 ||
  fail "observe rejected the starving measurement"
slo="$("$EP" slo --expert latency-balanced --socket "$SOCK")" || fail "slo after observe failed"
python3 - "$slo" <<'PY'
import json, sys
reply = json.loads(sys.argv[1])
assert reply.get("allowed") is False, reply
# Measured damage outranks stale host features: the workload signal is the more
# specific evidence, and the box is idle here so features would say insufficient.
assert reply.get("code") == "BG_PROGRESS_FLOOR", reply
assert reply.get("service_evidence") == "present", reply
print("[integration-fd] slo denied on measurement:", reply["code"])
PY

cat >"$TMP/obs-bad.json" <<JSON
{"workload": "svc-integration", "observed_at_ms": $NOW_MS, "window_ms": 5000,
 "expert": "latency-balanced", "requests_per_second": 12}
JSON
if "$EP" observe --file "$TMP/obs-bad.json" --socket "$SOCK" >/dev/null 2>&1; then
  fail "observe accepted an unknown measurement field"
fi

status="$("$EP" status --socket "$SOCK")" || fail "status after observe failed"
generation_before="$(field "$status" generation)"
python3 - "$status" <<'PY'
import json, sys
reply = json.loads(sys.argv[1])
service = reply.get("service", {})
assert service.get("observed") is True, reply
assert service.get("workload") == "svc-integration", service
assert service.get("bg_work_units_per_sec") == 0.01, service
assert isinstance(service.get("adverse_experts"), dict), service
assert service.get("outcome_pending") is False, service
assert "min_bg_work_units_per_sec" in service.get("declared_limits", {}), service
print("[integration-fd] service state reported:", service.get("measured_expert"))
PY
# Ingest is advisory: it must not become a second writer or bump the generation.
status="$("$EP" status --socket "$SOCK")" || fail "status recheck failed"
[ "$(field "$status" generation)" = "$generation_before" ] ||
  fail "service.observe moved the generation"

BG_OLD="$BG_PID"
kill -TERM "$BG_PID" 2>/dev/null || true
wait "$BG_PID" 2>/dev/null || true
BG_PID=""
sleep 2.5
cls_after="$("$CTL" class-get --pin-dir "$PINDIR" --pid "$BG_OLD")" || fail "class-get after exit failed"
python3 - "$cls_after" <<'PY'
import json, sys
reply = json.loads(sys.argv[1])
assert reply.get("present") is False, reply
print("[integration-fd] stale background member revoked")
PY

echo "[integration-fd] feature window"
sleep 1.1
features="$("$EP" features --socket "$SOCK")" || fail "features failed"
python3 - "$features" <<'PY'
import json, sys
reply = json.loads(sys.argv[1])
assert reply.get("status") == "OK", reply
assert reply.get("samples", 0) >= 1, reply
assert reply["metrics"]["load1"]["valid"] is True, reply
assert reply["metrics"]["run_delay_ns"]["valid"] is False, reply
assert reply["metrics"]["run_delay_ns"].get("missing"), reply
print("[integration-fd] features samples", reply["samples"])
PY

route="$("$EP" route --socket "$SOCK")" || fail "route failed"
python3 - "$route" <<'PY'
import json, sys
reply = json.loads(sys.argv[1])
assert reply.get("status") == "OK", reply
assert reply.get("expert") in {
    "safe-basic", "latency-balanced", "throughput-balanced", "cache-local"
}, reply
assert isinstance(reply.get("degraded"), bool), reply
print("[integration-fd] route", reply["expert"], "degraded", reply["degraded"])
PY

echo "[integration-fd] rollback"
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

echo "[integration-fd] crash + orphan recovery"
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
echo "[integration-fd] all checks passed"
