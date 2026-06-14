#!/usr/bin/env bash
set -euo pipefail

ROOT="/sys/fs/cgroup/eulerpilot"
LATENCY_WEIGHT="${LATENCY_WEIGHT:-1000}"
BATCH_WEIGHT="${BATCH_WEIGHT:-100}"
BACKGROUND_WEIGHT="${BACKGROUND_WEIGHT:-20}"

info() { printf '[INFO] %s\n' "$*"; }
fail() { printf '[FAIL] %s\n' "$*" >&2; exit 1; }

if ! mount | grep -q 'type cgroup2'; then
    fail 'cgroup v2 is not mounted on this system'
fi

mkdir -p "$ROOT"/latency "$ROOT"/batch "$ROOT"/background

if [ ! -d "$ROOT" ]; then
    fail "failed to create $ROOT"
fi

# Enable CPU controller for children when supported.
if [ -w /sys/fs/cgroup/cgroup.subtree_control ]; then
    if ! grep -q '\+cpu' /sys/fs/cgroup/cgroup.subtree_control 2>/dev/null; then
        echo +cpu > /sys/fs/cgroup/cgroup.subtree_control || true
    fi
    if ! grep -q '\+cpuset' /sys/fs/cgroup/cgroup.subtree_control 2>/dev/null; then
        echo +cpuset > /sys/fs/cgroup/cgroup.subtree_control || true
    fi
fi

if [ -w "$ROOT/cgroup.subtree_control" ]; then
    if ! grep -q '\+cpu' "$ROOT/cgroup.subtree_control" 2>/dev/null; then
        echo +cpu > "$ROOT/cgroup.subtree_control" || true
    fi
    if ! grep -q '\+cpuset' "$ROOT/cgroup.subtree_control" 2>/dev/null; then
        echo +cpuset > "$ROOT/cgroup.subtree_control" || true
    fi
fi

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

info "EulerPilot cgroup v2 hierarchy is ready under $ROOT"
info "  latency/cpu.weight=$LATENCY_WEIGHT"
info "  batch/cpu.weight=$BATCH_WEIGHT"
info "  background/cpu.weight=$BACKGROUND_WEIGHT"
info '  latency/cpuset.cpus=0-1'
info '  batch/cpuset.cpus=2-3'
info '  background/cpuset.cpus=4-7'
