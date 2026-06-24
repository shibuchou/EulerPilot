#!/usr/bin/env bash
set -euo pipefail

ROOT="/sys/fs/cgroup/eulerpilot"
LATENCY_WEIGHT="${LATENCY_WEIGHT:-1000}"
BATCH_WEIGHT="${BATCH_WEIGHT:-100}"
BACKGROUND_WEIGHT="${BACKGROUND_WEIGHT:-20}"

info() { printf '[INFO] %s\n' "$*"; }
fail() { printf '[FAIL] %s\n' "$*" >&2; exit 1; }

enable_controller() {
    local subtree="$1"
    local controller="$2"
    [ -w "$subtree" ] || return 0
    if ! grep -qw "$controller" "$subtree" 2>/dev/null; then
        echo "+$controller" > "$subtree" 2>/dev/null || true
    fi
}

if ! mount | grep -q 'type cgroup2'; then
    fail 'cgroup v2 is not mounted on this system'
fi

mkdir -p "$ROOT"/latency "$ROOT"/batch "$ROOT"/background

if [ ! -d "$ROOT" ]; then
    fail "failed to create $ROOT"
fi

# Enable controllers for children when supported. Memory is required by the
# Resource Control closed loop; failures are tolerated so doctor/test can give
# a clearer environment-specific error.
for controller in cpu cpuset memory; do
    enable_controller /sys/fs/cgroup/cgroup.subtree_control "$controller"
    enable_controller "$ROOT/cgroup.subtree_control" "$controller"
done

echo 0 > "$ROOT/cpuset.mems"
echo 0 > "$ROOT/latency/cpuset.mems"
echo 0 > "$ROOT/batch/cpuset.mems"
echo 0 > "$ROOT/background/cpuset.mems"

echo 0-1 > "$ROOT/latency/cpuset.cpus"
echo 2-3 > "$ROOT/batch/cpuset.cpus"
echo 4-7 > "$ROOT/background/cpuset.cpus"

echo "$LATENCY_WEIGHT" > "$ROOT/latency/cpu.weight"
echo "$BATCH_WEIGHT" > "$ROOT/batch/cpu.weight"
echo "$BACKGROUND_WEIGHT" > "$ROOT/background/cpu.weight"

for group in latency batch background; do
    group_path="$ROOT/$group"
    [ -w "$group_path/cpu.max" ] && echo max > "$group_path/cpu.max"
    [ -w "$group_path/memory.high" ] && echo max > "$group_path/memory.high"
    [ -w "$group_path/memory.low" ] && echo 0 > "$group_path/memory.low"
    [ -w "$group_path/memory.max" ] && echo max > "$group_path/memory.max"
done

info "EulerPilot cgroup v2 hierarchy is ready under $ROOT"
info "  latency/cpu.weight=$LATENCY_WEIGHT"
info "  batch/cpu.weight=$BATCH_WEIGHT"
info "  background/cpu.weight=$BACKGROUND_WEIGHT"
info '  memory controller requested for latency/batch/background'
info '  latency/cpuset.cpus=0-1'
info '  batch/cpuset.cpus=2-3'
info '  background/cpuset.cpus=4-7'
