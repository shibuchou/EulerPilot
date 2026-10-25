#!/usr/bin/env bash
# cgroup Skill integration on a real cgroup v2 hierarchy (kernel-integration layer).
#
# Proves the R5 action domain against the kernel's own file interface, not a fake tree:
# write with read-back, drift correction, compensation, and recovery of an effect that
# outlived the process which made it (cgroup files do not disappear when we crash).
#
#   integration_cgroup.sh <ep-executord> <ep>
set -euo pipefail

EXECUTORD="${1:?usage: integration_cgroup.sh <ep-executord> <ep>}"
EP="${2:?}"
SOURCE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

skip() { echo "[skip] $*"; exit 77; }
[ "$(id -u)" = "0" ] || skip "requires root"
[ "$(stat -fc %T /sys/fs/cgroup)" = "cgroup2fs" ] || skip "cgroup v2 (cgroup2fs) required"
command -v systemd-run >/dev/null || skip "systemd-run required"
[ -f "$SOURCE/design/examples/redis-policy.json" ] || skip "missing scheduling policy example"

UNIT="epv2-cgroup-skill-$$.scope"
CG="/sys/fs/cgroup/system.slice/$UNIT"
TMP="$(mktemp -d /tmp/ep-cgtest.XXXXXX)"
STATE="$TMP/state"
SOCK="$STATE/executor.sock"
DAEMON=""
MARKER="$STATE/cgroup-effects.json"
mkdir -p "$STATE"

cleanup() {
  local rc=$?
  [ -n "$DAEMON" ] && kill -TERM "$DAEMON" 2>/dev/null || true
  systemctl kill -s SIGKILL "$UNIT" 2>/dev/null || true
  wait 2>/dev/null || true
  if [ "$rc" -ne 0 ]; then
    echo "[FAIL] cgroup integration evidence retained: $TMP" >&2
  else
    rm -rf "$TMP"
  fi
}
trap cleanup EXIT

fail() { echo "[FAIL] $*" >&2; exit 1; }
field() { python3 -c 'import json,sys
try:
    print(json.loads(sys.argv[1]).get(sys.argv[2], ""))
except Exception:
    print("")' "$1" "$2"; }
weight() { cat "$CG/cpu.weight" 2>/dev/null | tr -d '[:space:]'; }

echo "[integration-cgroup] creating a systemd scope with CPUWeight=100"
systemd-run --scope --unit "$UNIT" -p CPUWeight=100 -- sleep 600 >"$TMP/scope.log" 2>&1 &
for _ in $(seq 1 100); do
  [ -f "$CG/cpu.weight" ] && break
  sleep 0.1
done
[ -f "$CG/cpu.weight" ] || fail "scope cgroup $CG never appeared"
[ "$(weight)" = 100 ] || fail "expected cpu.weight 100, got $(weight)"

start_daemon() {
  "$EXECUTORD" --state-dir "$STATE" --socket "$SOCK" --skill cgroup \
    >"$STATE/daemon.log" 2>&1 9>&- &
  DAEMON=$!
  for _ in $(seq 1 100); do
    "$EP" status --socket "$SOCK" >/dev/null 2>&1 && return 0
    kill -0 "$DAEMON" 2>/dev/null || break
    sleep 0.1
  done
  cat "$STATE/daemon.log"
  fail "cgroup executord did not become ready"
}

policy() { # <file> <weight> <ttl>
  python3 - "$1" "$2" "$3" "$CG" <<'PY'
import json, sys
out, weight, ttl, cg = sys.argv[1:5]
json.dump({
    "apiVersion": "eulerpilot.io/v1alpha1",
    "kind": "CgroupPolicy",
    "metadata": {"name": "integration-cgroup"},
    "spec": {
        "scope": "cgroup-effects",
        "source": "human",
        "effects": [{"path": cg, "cpu_weight": int(weight)}],
        "safety": {"policy_ttl_ms": int(ttl)},
    },
}, open(out, "w"))
PY
}

policy "$STATE/protect.json" 300 30000
start_daemon

echo "[integration-cgroup] validate"
validated="$("$EP" validate --file "$STATE/protect.json" --socket "$SOCK")"
[ "$(field "$validated" valid)" = "True" ] || fail "validate rejected a well-formed policy: $validated"
case "$validated" in *'"domain": "cgroup"'*) ;; *) fail "validate did not report the cgroup domain";; esac
python3 - "$validated" <<'PY'
import json, sys
d = json.loads(sys.argv[1])
raise SystemExit(0 if len(d.get("target_bindings") or []) == 1 else 1)
PY
[ "$?" = 0 ] || fail "expected exactly one target binding: $validated"

echo "[integration-cgroup] apply"
lease="$("$EP" lease --ttl-ms 60000 --socket "$SOCK")"
applied="$("$EP" apply --file "$STATE/protect.json" --idempotency-key idem-cg-0001 --ttl-ms 60000 --socket "$SOCK")"
status="$(field "$applied" status)"
[ "$status" = VERIFIED ] || fail "apply returned $status: $applied"
[ "$(weight)" = 300 ] || fail "kernel says $(weight), policy says 300"
[ -f "$MARKER" ] || fail "no recovery marker after an applied effect"
python3 -c 'import json,sys
m = json.load(open(sys.argv[1]))
raise SystemExit(0 if m["effects"][0]["previous"] == "100" else 1)' "$MARKER" \
  || fail "the marker did not record the pre-apply value"

echo "[integration-cgroup] out-of-band change is corrected"
echo 77 > "$CG/cpu.weight"
[ "$(weight)" = 77 ] || fail "could not simulate an out-of-band write"
corrected=0
for _ in $(seq 1 40); do
  if [ "$(weight)" = 300 ]; then corrected=1; break; fi
  sleep 0.25
done
[ "$corrected" = 1 ] || fail "the executor never restored cpu.weight (last $(weight))"
grep -q "cpu.weight drift corrected" "$STATE/daemon.log" \
  || fail "the drift correction happened without being reported"

echo "[integration-cgroup] rollback restores the pre-apply value"
rollback="$("$EP" rollback --of "$(field "$applied" action_id)" \
  --transaction "$(field "$applied" transaction_id)" --snapshot "$(field "$applied" snapshot_id)" \
  --policy-sha256 "$(field "$applied" policy_sha256)" --idempotency-key idem-cg-0001r \
  --socket "$SOCK")"
[ "$(field "$rollback" status)" = ROLLED_BACK ] || fail "rollback returned $rollback"
[ "$(weight)" = 100 ] || fail "cpu.weight stayed $(weight) after rollback"
[ ! -f "$MARKER" ] || fail "the recovery marker survived a clean rollback"

echo "[integration-cgroup] another domain is refused by this executor"
refused="$("$EP" apply --file "$SOURCE/design/examples/redis-policy.json" \
  --idempotency-key idem-cg-0002 --socket "$SOCK" 2>&1 || true)"
[ "$(field "$refused" status)" = REJECTED ] || fail "a SchedulingPolicy was not rejected: $refused"
case "$refused" in *UNSUPPORTED*) ;; *) fail "the rejection does not name the domain mismatch: $refused";; esac
[ "$(weight)" = 100 ] || fail "the rejected apply still changed the knob"

echo "[integration-cgroup] a crash leaves the effect behind; the restart must undo it"
applied2="$("$EP" apply --file "$STATE/protect.json" --idempotency-key idem-cg-0003 --ttl-ms 60000 --socket "$SOCK")"
[ "$(field "$applied2" status)" = VERIFIED ] || fail "second apply returned $applied2"
[ "$(weight)" = 300 ] || fail "second apply did not move the knob"
kill -KILL "$DAEMON"
DAEMON=""
sleep 1
[ "$(weight)" = 300 ] || fail "the cgroup forgot our value before the restart"
[ -f "$MARKER" ] || fail "the marker vanished with the process"
start_daemon
recovered=0
for _ in $(seq 1 40); do
  if [ "$(weight)" = 100 ]; then recovered=1; break; fi
  sleep 0.25
done
[ "$recovered" = 1 ] || fail "restart did not restore cpu.weight (last $(weight))"
[ ! -f "$MARKER" ] || fail "the recovered effect kept its marker"
grep -q "recovered 1 cgroup effect" "$STATE/daemon.log" \
  || echo "[integration-cgroup] note: recovery happened without a detail line in this log"

echo "[integration-cgroup] passing"
