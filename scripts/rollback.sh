#!/usr/bin/env bash
set -euo pipefail

SCX_BIN="${EULERPILOT_SCX_BINARY:-$(command -v scx_eulerpilot 2>/dev/null || true)}"
SCX_BIN="${SCX_BIN:-/usr/local/bin/scx_eulerpilot}"
SCX_NAME="$(basename "$SCX_BIN")"
echo "[rollback] scx binary: $SCX_BIN" >&2

ROOT="/sys/fs/cgroup/eulerpilot"
detect_root_io_device() {
    findmnt -no MAJ:MIN -T / 2>/dev/null | awk 'NF { print $1; exit }'
}

IO_DEVICE="${IO_DEVICE:-$(detect_root_io_device)}"

printf '[Rollback] restore cgroup parameters and stop active sched_ext scheduler if configured.\n'

if [ -x "$SCX_BIN" ]; then
    "$SCX_BIN" --detach >/dev/null 2>&1 || true
else
    pkill -x "$SCX_NAME" 2>/dev/null || true
fi

rm -f /sys/fs/bpf/eulerpilot/scx_eulerpilot/v1/class_map 2>/dev/null || true
rm -f /sys/fs/bpf/eulerpilot/scx_eulerpilot/v1/gate_state_map 2>/dev/null || true
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
        [ -w "$group_path/io.weight" ] && echo "default 100" > "$group_path/io.weight" 2>/dev/null || true
        if [ -n "$IO_DEVICE" ] && [ -w "$group_path/io.max" ]; then
            echo "$IO_DEVICE rbps=max wbps=max riops=max wiops=max" > "$group_path/io.max" 2>/dev/null || true
        fi
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
