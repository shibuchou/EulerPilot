#!/usr/bin/env bash
set -euo pipefail

[ $# -eq 1 ] || { printf 'usage: %s <output-dir>\n' "$(basename "$0")" >&2; exit 1; }

OUTDIR="$1"
mkdir -p "$OUTDIR"

{
    printf 'timestamp=%s\n' "$(date --iso-8601=seconds)"
    printf 'hostname=%s\n' "$(hostname)"
    printf 'kernel=%s\n' "$(uname -r)"
    printf 'cpu_count=%s\n' "$(nproc)"
    printf 'cmdline=%s\n' "$(cat /proc/cmdline)"
} > "$OUTDIR/system.env"

cp /proc/pressure/cpu "$OUTDIR/pressure_cpu.txt"
mount | grep cgroup > "$OUTDIR/cgroup_mounts.txt" || true

if [ -d /sys/fs/cgroup/eulerpilot ]; then
    find /sys/fs/cgroup/eulerpilot -maxdepth 2 -type f \
        \( -name cpu.weight -o -name cgroup.procs -o -name cgroup.controllers -o -name cgroup.subtree_control \) \
        -print0 | while IFS= read -r -d '' file; do
            safe_name="$(echo "$file" | sed 's#^/##; s#[/#]#_#g')"
            cat "$file" > "$OUTDIR/$safe_name" 2>/dev/null || true
        done
fi
