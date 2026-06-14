#!/usr/bin/env bash
set -euo pipefail

ROOT="/sys/fs/cgroup/eulerpilot"

printf '[Rollback] restore cgroup parameters and stop active sched_ext scheduler if configured.\n'

pkill -f '/root/olk/kernel-OLK-6.6-atomgit/tools/sched_ext/build/bin/scx_' 2>/dev/null || true
sleep 1

rm -f /sys/fs/bpf/class_map 2>/dev/null || true
rm -f /sys/fs/bpf/stats 2>/dev/null || true
rm -f /sys/fs/bpf/eulerpilot/scx_eulerpilot/v1/class_map 2>/dev/null || true
rm -f /sys/fs/bpf/eulerpilot/scx_eulerpilot/v1/stats 2>/dev/null || true
rmdir /sys/fs/bpf/eulerpilot/scx_eulerpilot/v1 2>/dev/null || true
rmdir /sys/fs/bpf/eulerpilot/scx_eulerpilot 2>/dev/null || true
rmdir /sys/fs/bpf/eulerpilot 2>/dev/null || true

if mount | grep -q 'type cgroup2' && [ -d "$ROOT" ]; then
    for group in latency batch background; do
        if [ -f "$ROOT/$group/cgroup.procs" ]; then
            while read -r pid; do
                [ -n "$pid" ] && echo "$pid" > /sys/fs/cgroup/cgroup.procs 2>/dev/null || true
            done < "$ROOT/$group/cgroup.procs"
        fi
    done

    rmdir "$ROOT/latency" 2>/dev/null || true
    rmdir "$ROOT/batch" 2>/dev/null || true
    rmdir "$ROOT/background" 2>/dev/null || true
    rmdir "$ROOT" 2>/dev/null || true
    printf '[Rollback] cgroup v2 hierarchy cleaned if it was idle.\n'
else
    printf '[Rollback] no EulerPilot cgroup v2 hierarchy detected.\n'
fi

if [ -d /sys/kernel/sched_ext ]; then
    printf '[Rollback] sched_ext state: %s\n' "$(cat /sys/kernel/sched_ext/state 2>/dev/null || echo unknown)"
fi
