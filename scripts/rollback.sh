#!/usr/bin/env bash
set -euo pipefail

SCX_BIN="${EULERPILOT_SCX_BINARY:-/root/olk/kernel-OLK-6.6-atomgit/tools/sched_ext/build/bin/scx_eulerpilot}"
SCX_NAME="$(basename "$SCX_BIN")"
echo "[rollback] scx binary: $SCX_BIN" >&2

ROOT="/sys/fs/cgroup/eulerpilot"

printf '[Rollback] restore cgroup parameters and stop active sched_ext scheduler if configured.\n'

pkill -f "$SCX_BIN" 2>/dev/null || true
pkill -f "$SCX_NAME" 2>/dev/null || true

# legacy path fallback
pkill -f '/root/olk/kernel-OLK-6.6-atomgit/tools/sched_ext/build/bin/scx_' 2>/dev/null || true

rm -f /sys/fs/bpf/eulerpilot/scx_eulerpilot/v1/class_map 2>/dev/null || true
rm -f /sys/fs/bpf/eulerpilot/scx_eulerpilot/v1/stats 2>/dev/null || true
rmdir /sys/fs/bpf/eulerpilot/scx_eulerpilot/v1 2>/dev/null || true
rmdir /sys/fs/bpf/eulerpilot/scx_eulerpilot 2>/dev/null || true

if [ -d "$ROOT" ]; then
    for subgroup in latency batch background; do
        group_path="$ROOT/$subgroup"
        [ -w "$group_path/cpu.max" ] && echo max > "$group_path/cpu.max" 2>/dev/null || true
        [ -w "$group_path/memory.high" ] && echo max > "$group_path/memory.high" 2>/dev/null || true
        [ -w "$group_path/memory.low" ] && echo 0 > "$group_path/memory.low" 2>/dev/null || true
        [ -w "$group_path/memory.max" ] && echo max > "$group_path/memory.max" 2>/dev/null || true
        if [ -f "$group_path/cgroup.procs" ]; then
            while read -r pid; do
                [ -n "$pid" ] && echo "$pid" > /sys/fs/cgroup/cgroup.procs 2>/dev/null || true
            done < "$group_path/cgroup.procs"
        fi
        rmdir "$group_path" 2>/dev/null || true
    done
    rmdir "$ROOT" 2>/dev/null || true
fi

printf '[Rollback] done.\n'
