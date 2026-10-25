#!/usr/bin/env bash
# R5 event link integration on a real kernel (kernel-integration layer).
#
# Proves the second half of R5 as one timeline on the real tracing subsystem:
#   hook (sched_wakeup/sched_switch BPF) -> pump (ep-evtmon) -> event.observe
#   -> armed decision -> cpu.weight apply through the transaction path ->
#   compensation when the condition clears, and when the pump dies the gate
#   freezes shut and the policy TTL undoes the effect.
#
# Load design: the noise cgroup runs pure CPU sponges (they saturate CPUs but
# barely wake up), the watched cgroup runs periodic sleepers (every wake is a
# sched_wakeup that must wait behind the sponges). The measured signal is
# wakeup-to-first-run delay, so only the sleepers produce the condition.
#
#   integration_events.sh <ep-executord> <ep> <ep-evtmon>
set -euo pipefail

EXECUTORD="${1:?usage: integration_events.sh <ep-executord> <ep> <ep-evtmon>}"
EP="${2:?}"
EVTMON="${3:?}"

skip() { echo "[skip] $*"; exit 77; }
[ "$(id -u)" = "0" ] || skip "requires root"
[ "$(stat -fc %T /sys/fs/cgroup)" = "cgroup2fs" ] || skip "cgroup v2 (cgroup2fs) required"
command -v systemd-run >/dev/null || skip "systemd-run required (it owns the cpu controller enablement)"
[ -x "$EVTMON" ] || skip "ep-evtmon was not built (needs libbpf and the events skeleton)"
[ -e /sys/kernel/tracing/events/sched/sched_wakeup ] || skip "sched tracepoints unavailable"

UNIT="epv2-evt-$$.scope"
TAG="epv2-evt-$$"
CG="/sys/fs/cgroup/system.slice/$UNIT"
CG_NOISE="/sys/fs/cgroup/system.slice/$TAG-noise"
TMP="$(mktemp -d /tmp/ep-evttest.XXXXXX)"
STATE="$TMP/state"
mkdir -p "$STATE"
SOCK="$STATE/executor.sock"
DAEMON=""
PUMP=""
ALL_PIDS=()
NOISE_PIDS=()

cleanup() {
  local rc=$?
  for pid in "${ALL_PIDS[@]:-}"; do kill -KILL "$pid" 2>/dev/null || true; done
  [ -n "$PUMP" ] && kill -TERM "$PUMP" 2>/dev/null || true
  [ -n "$DAEMON" ] && kill -TERM "$DAEMON" 2>/dev/null || true
  systemctl kill -s SIGKILL "$UNIT" 2>/dev/null || true
  wait 2>/dev/null || true
  rmdir "$CG_NOISE" 2>/dev/null || true
  if [ "$rc" -ne 0 ]; then
    echo "[FAIL] event integration evidence retained: $TMP" >&2
  else
    rm -rf "$TMP"
  fi
}
trap cleanup EXIT

fail() { echo "[FAIL] $*" >&2; exit 1; }
field() { python3 -c 'import json,sys
try:
    value = json.loads(sys.argv[1])
    for key in sys.argv[2].split("."):
        value = value[key]
    print(value)
except Exception:
    print("")' "$1" "$2"; }
weight() { cat "$CG/cpu.weight" 2>/dev/null | tr -d '[:space:]'; }

sponges() { # <cgroup> <count> -- saturate CPUs, do not wake: this is contention.
  # Reniced to -10 so a woken sleeper demonstrably waits behind them: at equal
  # priority the steady-state wait lands at 3.8-4.3ms, right under the 5ms
  # threshold, and the test would flap on which side a window falls.
  local i
  for i in $(seq 1 "$2"); do
    setsid nice -n -10 bash -c 'while :; do :; done' >/dev/null 2>&1 &
    ALL_PIDS+=($!)
    NOISE_PIDS+=($!)
    echo $! > "$1/cgroup.procs"
  done
}
sleepers() { # <cgroup> <count> -- wake every 20ms: this is the measured signal
  local i
  for i in $(seq 1 "$2"); do
    setsid bash -c 'while :; do sleep 0.02; done' >/dev/null 2>&1 &
    ALL_PIDS+=($!)
    echo $! > "$1/cgroup.procs"
  done
}
kill_noise() {
  local pid
  for pid in "${NOISE_PIDS[@]:-}"; do kill -KILL "$pid" 2>/dev/null || true; done
  NOISE_PIDS=()
}

echo "[integration-events] creating the watched scope (CPUWeight=100) and the noise cgroup"
# A scope declared with CPUWeight makes systemd enable the cpu controller on its
# path; a bare mkdir under the root would have no cpu.weight at all (root and
# system.slice only delegate memory pids by default on this system).
systemd-run --scope --unit "$UNIT" -p CPUWeight=100 -- sleep 600 >"$TMP/scope.log" 2>&1 &
for _ in $(seq 1 100); do
  [ -f "$CG/cpu.weight" ] && break
  sleep 0.1
done
[ -f "$CG/cpu.weight" ] || fail "scope cgroup $CG never appeared: $(cat "$TMP/scope.log")"
mkdir "$CG_NOISE" || fail "cannot create $CG_NOISE"
[ "$(weight)" = 100 ] || fail "expected cpu.weight 100, got $(weight)"

# Threshold 5ms: an idle box resolves wakeup-to-run at ~0.1ms, while woken
# sleepers behind eight sponges wait tens of milliseconds. One window either
# way, and a 12s policy TTL so the pump-death phase ends inside the budget.
python3 - "$STATE/arm.json" "$CG" <<'PY'
import json, sys
json.dump({
    "apiVersion": "eulerpilot.io/v1alpha1",
    "kind": "EventArm",
    "metadata": {"name": "integration-events"},
    "spec": {
        "watch": sys.argv[2],
        "threshold_wait_ms": 5,
        "sustain_windows": 1,
        "release_windows": 1,
        "min_interval_ms": 1000,
        "lease_ttl_ms": 30000,
        "policy_ttl_ms": 12000,
        "action": {"cpu_weight": 5000},
    },
}, open(sys.argv[1], "w"))
PY

echo "[integration-events] starting the armed cgroup executor and the pump"
"$EXECUTORD" --state-dir "$STATE" --socket "$SOCK" --skill cgroup \
  --event-arm "$STATE/arm.json" >"$STATE/daemon.log" 2>&1 9>&- &
DAEMON=$!
for _ in $(seq 1 100); do
  "$EP" status --socket "$SOCK" >/dev/null 2>&1 && break
  sleep 0.1
done
grep -q "event guard armed" "$STATE/daemon.log" || fail "the executor did not arm: $(cat "$STATE/daemon.log")"

"$EVTMON" --socket "$SOCK" --window-ms 3000 --interval-ms 500 >"$TMP/pump.log" 2>&1 9>&- &
PUMP=$!
for _ in $(seq 1 50); do
  grep -q '"source":"evtmon"' "$TMP/pump.log" 2>/dev/null && break
  sleep 0.2
done
grep -q '"source":"evtmon"' "$TMP/pump.log" || fail "the pump produced no report: $(cat "$TMP/pump.log")"

poll_weight() { # <expected> <seconds> <label>
  local deadline=$((SECONDS + $2))
  while [ $SECONDS -lt $deadline ]; do
    [ "$(weight)" = "$1" ] && return 0
    sleep 1
  done
  return 1
}

echo "[integration-events] phase A: idle watch must not act"
sleepers "$CG" 1
sleep 8
[ "$(weight)" = 100 ] || fail "the guard acted without a condition (weight $(weight))"
status="$("$EP" status --socket "$SOCK")"
case "$(field "$status" event.last_hold)" in
  *threshold*|*events*|*EVENT_OBSERVATION*) ;;
  *) fail "unexpected hold while idle: $(field "$status" event.last_hold)";;
esac

echo "[integration-events] phase B: runnable starvation raises cpu.weight"
sponges "$CG_NOISE" 8
sleepers "$CG" 24
poll_weight 5000 40 || fail "cpu.weight never rose (last $(weight), hold: $(field "$("$EP" status --socket "$SOCK")" event.last_hold))"
grep -q "event guard applied cpu.weight=5000" "$STATE/daemon.log" \
  || fail "the apply happened without the guard logging it"
echo "[integration-events] applied at $(date +%T)"

echo "[integration-events] phase C: condition clears, weight is compensated back"
kill_noise
poll_weight 100 40 || fail "cpu.weight never compensated (last $(weight))"
grep -q "event guard rolled back" "$STATE/daemon.log" \
  || fail "the rollback happened without the guard logging it"
echo "[integration-events] compensated at $(date +%T)"

echo "[integration-events] phase D: pump death freezes the gate; TTL undoes the effect"
sponges "$CG_NOISE" 8
poll_weight 5000 40 || fail "re-starvation did not re-apply (weight $(weight), hold: $(field "$("$EP" status --socket "$SOCK")" event.last_hold))"
kill -TERM "$PUMP"; PUMP=""
sleep 2
poll_weight 100 25 || fail "the effect outlived both the pump and its TTL (weight $(weight))"
status="$("$EP" status --socket "$SOCK")"
case "$(field "$status" event.last_hold)" in
  *EVENT_OBSERVATION_STALE*) ;;
  *) fail "a dead pump is not visible as STALE: $(field "$status" event.last_hold)";;
esac
[ "$(field "$status" event.stale_checks)" -ge 1 ] 2>/dev/null \
  || fail "stale_checks did not accumulate: $status"
[ "$(weight)" = 100 ] || fail "weight drifted after the TTL rollback ($(weight))"
kill_noise

echo "[integration-events] phase F: a rebuilt watch identity freezes the arm"
# Kill the scope (its cgroup disappears) and recreate the same path as a plain
# directory: same name, different kernfs identity. The arm named the old
# object, so the guard must refuse to act on the replacement even under
# starvation, and say so in the hold.
systemctl kill -s SIGKILL "$UNIT" 2>/dev/null || true
for _ in $(seq 1 40); do [ ! -d "$CG" ] && break; sleep 0.25; done
[ ! -d "$CG" ] || fail "the scope cgroup did not disappear"
mkdir "$CG" 2>/dev/null || fail "cannot recreate $CG as a plain directory"
"$EVTMON" --socket "$SOCK" --window-ms 3000 --interval-ms 500 >"$TMP/pump-f.log" 2>&1 9>&- &
PUMP=$!
for _ in $(seq 1 50); do
  grep -q "\"source\":\"evtmon\"" "$TMP/pump-f.log" 2>/dev/null && break
  sleep 0.2
done
sponges "$CG_NOISE" 8
sleepers "$CG" 8
applied_lines_before=$(grep -c "event guard applied" "$STATE/daemon.log")
sleep 14
status="$("$EP" status --socket "$SOCK")"
case "$(field "$status" event.last_hold)" in
  *WATCH_REBUILT*) ;;
  *) fail "a rebuilt watch is not visible as WATCH_REBUILT: $(field "$status" event.last_hold)";;
esac
[ "$(grep -c "event guard applied" "$STATE/daemon.log")" = "$applied_lines_before" ] ||
  fail "the guard acted on a rebuilt cgroup"
kill -TERM "$PUMP"; PUMP=""
kill_noise
kill -KILL "${ALL_PIDS[@]}" 2>/dev/null || true
rmdir "$CG" 2>/dev/null || true

echo "[integration-events] phase E: the pump alone writes nothing"
kill -TERM "$DAEMON"; DAEMON=""
sleep 2
# Phase F left the watched path as a plain directory (no cpu.* files), so the
# assertion is the stronger one: the pump does not create, remove or change
# anything under the watched path while running alone.
watch_listing_before="$(ls -A "$CG" 2>/dev/null | sort)"
"$EVTMON" --print-only --window-ms 2000 --interval-ms 500 >"$TMP/pump-solo.log" 2>&1 9>&- &
PUMP=$!
sleep 7
kill -TERM "$PUMP"; PUMP=""
watch_listing_after="$(ls -A "$CG" 2>/dev/null | sort)"
[ "$watch_listing_before" = "$watch_listing_after" ]   || fail "the pump alone changed the watched cgroup (before: '$watch_listing_before' after: '$watch_listing_after')"
[ ! -f "$CG/cpu.weight" ] || fail "the pump alone created cpu.weight"

echo "[integration-events] passing"
