#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
EXECUTORS="$ROOT/agent/src/executors.cpp"
HEADER="$ROOT/agent/include/executors.hpp"
ROLLBACK="$ROOT/scripts/rollback.sh"

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

[ -f "$EXECUTORS" ] || fail "missing executors.cpp"
[ -f "$HEADER" ] || fail "missing executors.hpp"
[ -f "$ROLLBACK" ] || fail "missing rollback.sh"

grep -q 'pid_start_time_ticks' "$HEADER" ||
    fail "ScxSession does not record process start time"
grep -q 'executable_dev' "$HEADER" ||
    fail "ScxSession does not record executable device"
grep -q 'executable_ino' "$HEADER" ||
    fail "ScxSession does not record executable inode"
grep -q 'instance_id' "$HEADER" ||
    fail "ScxSession does not expose an instance id"

grep -q 'read_proc_start_time_ticks' "$EXECUTORS" ||
    fail "ScxExecutor does not read /proc starttime"
grep -q 'stat_proc_executable' "$EXECUTORS" ||
    fail "ScxExecutor does not stat /proc/<pid>/exe"
grep -q 'scx_session_identity_matches' "$EXECUTORS" ||
    fail "ScxExecutor does not verify owned scheduler identity"
grep -q 'scx-external-instance-active' "$EXECUTORS" ||
    fail "ScxExecutor does not refuse unknown active sched_ext instance"
grep -q 'refuse-stop' "$EXECUTORS" ||
    fail "stop_scx_session does not refuse stop on identity mismatch"
grep -q 'refuse-kill' "$EXECUTORS" ||
    fail "stop_scx_session does not refuse SIGKILL on identity mismatch"

if grep -qE 'pkill|killall' "$EXECUTORS"; then
    fail "ScxExecutor contains process-name kill fallback"
fi

if grep -v 'refusing pkill fallback' "$ROLLBACK" |
    grep -qE '(^|[;&|[:space:]])(pkill|killall)([[:space:]]|$)'; then
    fail "rollback.sh contains process-name kill fallback"
fi

grep -q 'safe scx detach failed; preserving pinned scx maps' "$ROLLBACK" ||
    fail "rollback.sh does not preserve maps on safe detach failure"
grep -q 'refusing pkill fallback' "$ROLLBACK" ||
    fail "rollback.sh does not explicitly refuse pkill fallback"
grep -Fq 'if [ "$scx_detach_ok" -eq 1 ]; then' "$ROLLBACK" ||
    fail "rollback.sh does not guard pinned map deletion behind safe detach success"

python3 - "$EXECUTORS" "$ROLLBACK" <<'PY'
import re
import sys
from pathlib import Path

executors = Path(sys.argv[1]).read_text(encoding="utf-8", errors="replace")
rollback = Path(sys.argv[2]).read_text(encoding="utf-8", errors="replace")

start = executors.find("bool start_scx_session(")
stop = executors.find("void stop_scx_session(")
reconcile = executors.find("bool reconcile_scx_session(")
if start < 0 or stop < 0 or reconcile < 0:
    raise SystemExit("missing SCX session lifecycle functions")
start_body = executors[start:stop]
stop_body = executors[stop:reconcile]

ordered_start = [
    "read_file_trimmed(\"/sys/kernel/sched_ext/state\") == \"enabled\"",
    "reason = \"scx-external-instance-active\"",
    "stat_path_identity(session.binary_path, expected_exe_dev, expected_exe_ino)",
    "session.pid = pid;",
    "session.executable_dev = expected_exe_dev;",
    "session.executable_ino = expected_exe_ino;",
    "scx_session_identity_matches(session, identity_reason)",
]
cursor = 0
for token in ordered_start:
    pos = start_body.find(token, cursor)
    if pos < 0:
        raise SystemExit(f"start_scx_session missing or out of order: {token}")
    cursor = pos + len(token)

ordered_stop = [
    "scx_session_identity_matches(session, identity_reason)",
    "kill(session.pid, SIGTERM)",
    "scx_session_identity_matches(session, kill_reason)",
    "kill(owned_pid, SIGKILL)",
]
cursor = 0
for token in ordered_stop:
    pos = stop_body.find(token, cursor)
    if pos < 0:
        raise SystemExit(f"stop_scx_session missing or out of order: {token}")
    cursor = pos + len(token)

detach_guard = re.search(
    r'if \[ "\$scx_detach_ok" -eq 1 \]; then(?P<body>.*?)\nfi',
    rollback,
    re.S,
)
if not detach_guard:
    raise SystemExit("rollback.sh missing scx_detach_ok guard")
guard_body = detach_guard.group("body")
for token in ("class_map", "gate_state_map", "stats"):
    if token not in guard_body:
        raise SystemExit(f"rollback.sh map cleanup missing from guarded block: {token}")
PY

echo "scx_loader_ownership=pass"
