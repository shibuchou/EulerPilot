#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
EXECUTORS="$ROOT/agent/src/executors.cpp"

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

[ -f "$EXECUTORS" ] || fail "missing executors.cpp"

grep -q 'restore_resource_snapshot' "$EXECUTORS" ||
    fail "missing ResourceControl restore_resource_snapshot"

grep -q 'compare-before-restore-mismatch' "$EXECUTORS" ||
    fail "rollback does not compare current value before restore"

grep -q 'ownership_mismatch' "$EXECUTORS" ||
    fail "rollback does not check cgroup identity before restore"

grep -q 'rollback_resource_control_keys(transaction_keys)' "$EXECUTORS" ||
    fail "partial apply failure does not roll back successful steps"

grep -q 'memory-reclaim-not-reversible-disabled' "$EXECUTORS" ||
    fail "memory.reclaim is not fail-closed as non-reversible"

python3 - "$EXECUTORS" <<'PY'
import re
import sys
from pathlib import Path

text = Path(sys.argv[1]).read_text(encoding="utf-8", errors="replace")

restore = re.search(
    r"bool restore_resource_snapshot\(const ControlFileSnapshot &snapshot\) \{(?P<body>.*?)\n\}",
    text,
    re.S,
)
if not restore:
    raise SystemExit("restore_resource_snapshot body missing")
body = restore.group("body")
required_order = [
    "stat_cgroup_identity",
    "control_value_matches(snapshot.file, snapshot.desired_value, current_value)",
    "write_file_value(snapshot.path, snapshot.restore_value)",
]
cursor = 0
for token in required_order:
    found = body.find(token, cursor)
    if found < 0:
        raise SystemExit(f"restore_resource_snapshot missing or out of order: {token}")
    cursor = found + len(token)

rollback = re.search(
    r"bool rollback_resource_control_keys\(const std::vector<std::string> &keys\) \{(?P<body>.*?)\n\}",
    text,
    re.S,
)
if not rollback:
    raise SystemExit("rollback_resource_control_keys body missing")
rollback_body = rollback.group("body")
if "resource_control_snapshots().erase(snapshot_it)" not in rollback_body:
    raise SystemExit("successful rollback does not erase restored snapshot")
if not re.search(r"else\s*\{\s*ok = false;\s*\}", rollback_body):
    raise SystemExit("failed rollback does not preserve snapshot and report failure")

start = text.find("ExecutionAction apply_cgroup_assignment(")
end = text.find("bool rollback_resource_control_state()", start)
if start < 0 or end < 0:
    raise SystemExit("apply_cgroup_assignment body missing")
apply_body = text[start:end]
if "auto fail_after_partial_apply" not in apply_body:
    raise SystemExit("apply_cgroup_assignment missing partial apply rollback helper")
if "rollback_resource_control_keys(transaction_keys);" not in apply_body:
    raise SystemExit("partial apply rollback helper does not restore transaction keys")
if "memory-reclaim-not-reversible-disabled" not in apply_body:
    raise SystemExit("memory.reclaim not isolated from reversible transaction")
PY

echo "resource_control_rollback_model=pass"
