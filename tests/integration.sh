#!/usr/bin/env bash
# End-to-end test: daemon + CLI over the Unix socket, including a hard kill,
# restart, second-instance rejection and a synthetic crash-reconcile.
set -euo pipefail

EXECUTORD="${1:?usage: integration.sh <ep-executord> <ep>}"
EP="${2:?usage: integration.sh <ep-executord> <ep>}"

TMP="$(mktemp -d /tmp/ep-integration.XXXXXX)"
STATE="$TMP/state"
SOCK="$TMP/executor.sock"
DAEMON_PID=""

fail() {
  echo "[FAIL] $*" >&2
  if [ -f "$TMP/daemon.log" ]; then
    tail -n 40 "$TMP/daemon.log" >&2 || true
  fi
  exit 1
}

cleanup() {
  if [ -n "$DAEMON_PID" ] && kill -0 "$DAEMON_PID" 2>/dev/null; then
    kill -TERM "$DAEMON_PID" 2>/dev/null || true
    wait "$DAEMON_PID" 2>/dev/null || true
  fi
  rm -rf "$TMP"
}
trap cleanup EXIT

start_daemon() {
  "$EXECUTORD" --state-dir "$STATE" --socket "$SOCK" --test-mode >>"$TMP/daemon.log" 2>&1 &
  DAEMON_PID=$!
  for _ in $(seq 1 100); do
    if [ -S "$SOCK" ]; then return 0; fi
    if ! kill -0 "$DAEMON_PID" 2>/dev/null; then break; fi
    sleep 0.05
  done
  fail "daemon did not create $SOCK"
}

stop_daemon() {
  if [ -n "$DAEMON_PID" ]; then
    kill -TERM "$DAEMON_PID" 2>/dev/null || true
    wait "$DAEMON_PID" 2>/dev/null || true
    DAEMON_PID=""
  fi
}

field() {
  python3 -c 'import json,sys; d=json.loads(sys.argv[1]); v=d.get(sys.argv[2],""); print(v)' \
    "$1" "$2"
}

expect_status() {
  local response="$1" expected="$2" label="$3"
  local actual
  actual="$(field "$response" status)"
  [ "$actual" = "$expected" ] || fail "$label: expected $expected, got $actual: $response"
}

read_value() {
  python3 - "$STATE/mock/value.json" <<'PY'
import json,sys,os
path=sys.argv[1]
print(json.load(open(path))["value"] if os.path.exists(path) else "absent")
PY
}

echo "[integration] state=$STATE"

start_daemon
status="$("$EP" status --socket "$SOCK")" || fail "status failed"
expect_status "$status" OK "initial status"
[ "$(field "$status" degraded)" = "False" ] || fail "daemon starts degraded"
[ "$(field "$status" generation)" = "0" ] || fail "initial generation is not 0"

resp="$("$EP" apply --intent '{"value":42}' --idempotency-key idem-int-0001 --socket "$SOCK")" ||
  fail "apply 42 failed"
expect_status "$resp" VERIFIED "apply 42"
[ "$(field "$resp" observed_generation)" = "1" ] || fail "apply 42 generation is not 1"
[ "$(read_value)" = "42" ] || fail "mock value is not 42"
records_after_apply="$(field "$("$EP" status --socket "$SOCK")" journal_records)"

replay="$("$EP" apply --intent '{"value":42}' --idempotency-key idem-int-0001 --socket "$SOCK")" ||
  fail "idempotent replay failed"
expect_status "$replay" VERIFIED "idempotent replay"
[ "$(field "$replay" observed_generation)" = "1" ] || fail "replay advanced generation"
status="$("$EP" status --socket "$SOCK")"
[ "$(field "$status" journal_records)" = "$records_after_apply" ] ||
  fail "replay appended journal records"

resp2="$("$EP" apply --intent '{"value":7}' --idempotency-key idem-int-0002 --socket "$SOCK")" ||
  fail "apply 7 failed"
expect_status "$resp2" VERIFIED "apply 7"
[ "$(field "$resp2" observed_generation)" = "2" ] || fail "apply 7 generation is not 2"
[ "$(read_value)" = "7" ] || fail "mock value is not 7"

ACTION="$(field "$resp2" action_id)"
SNAPSHOT="$(field "$resp2" snapshot_id)"
TXN="$(field "$resp2" transaction_id)"
DIGEST="$(field "$resp2" policy_sha256)"
[ -n "$ACTION" ] && [ -n "$SNAPSHOT" ] && [ -n "$TXN" ] || fail "apply response lacks ids"

rollback="$("$EP" rollback --of "$ACTION" --transaction "$TXN" --snapshot "$SNAPSHOT" \
  --policy-sha256 "$DIGEST" --socket "$SOCK")" || fail "rollback failed"
expect_status "$rollback" ROLLED_BACK "rollback"
[ "$(field "$rollback" observed_generation)" = "3" ] || fail "rollback generation is not 3"
[ "$(read_value)" = "42" ] || fail "rollback did not restore 42"

echo "[integration] restart persistence"
stop_daemon
start_daemon
status="$("$EP" status --socket "$SOCK")" || fail "status after restart failed"
expect_status "$status" OK "status after restart"
[ "$(field "$status" generation)" = "3" ] || fail "generation lost across restart"
[ "$(field "$status" degraded)" = "False" ] || fail "restart is degraded"

echo "[integration] second instance must be rejected"
"$EXECUTORD" --state-dir "$STATE" --socket "$TMP/second.sock" --test-mode \
  >"$TMP/second.log" 2>&1 &
SECOND_PID=$!
sleep 1
if kill -0 "$SECOND_PID" 2>/dev/null; then
  kill -KILL "$SECOND_PID" 2>/dev/null || true
  fail "second executor instance started"
fi
wait "$SECOND_PID" 2>/dev/null || true
grep -q "another executor" "$TMP/second.log" || fail "second instance did not report the lock"

echo "[integration] synthetic crash reconcile"
stop_daemon
python3 - "$STATE" <<'PY'
import json, os, sys, time
state = sys.argv[1]
path = os.path.join(state, "journal.jsonl")
seq = 0
if os.path.exists(path):
    for line in open(path):
        line = line.strip()
        if line:
            seq = json.loads(line)["seq"]
now = time.strftime("%Y-%m-%dT%H:%M:%S.000Z", time.gmtime())
records = [
    {"seq": seq + 1, "ts": now, "txn": "txn-crash-int-01", "action_id": "act-crash-int-01",
     "type": "PREPARED", "generation": 3, "owner": "test", "fence": 1,
     "data": {"intent": {"value": 99}, "snapshot": {"exists": True, "value": 42},
              "snapshot_id": "snap-crash-int-01", "policy_sha256": "a" * 64}},
    {"seq": seq + 2, "ts": now, "txn": "txn-crash-int-01", "action_id": "act-crash-int-01",
     "type": "APPLYING", "generation": 3, "owner": "test", "fence": 1, "data": {}},
]
with open(path, "a") as handle:
    for record in records:
        handle.write(json.dumps(record) + "\n")
with open(os.path.join(state, "mock", "value.json"), "w") as handle:
    handle.write('{"value":99}\n')
PY
start_daemon
status="$("$EP" status --socket "$SOCK")" || fail "status after reconcile failed"
expect_status "$status" OK "status after reconcile"
[ "$(field "$status" degraded)" = "False" ] || fail "reconcile left the executor degraded"
[ "$(read_value)" = "42" ] || fail "reconcile did not restore the snapshot"
[ "$(field "$status" generation)" = "3" ] || fail "reconcile changed the generation"

echo "[integration] all checks passed"
