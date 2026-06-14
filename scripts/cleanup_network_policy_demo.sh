#!/usr/bin/env bash
set -euo pipefail

DEMO_CGROUP="/sys/fs/cgroup/eulerpilot/demo-net"

bpftool cgroup detach "$DEMO_CGROUP" connect4 pinned /sys/fs/bpf/network_policy_demo 2>/dev/null || true
if [ -d "$DEMO_CGROUP" ]; then
    while read -r pid; do
        [ -n "$pid" ] && echo "$pid" > /sys/fs/cgroup/cgroup.procs 2>/dev/null || true
    done < "$DEMO_CGROUP/cgroup.procs"
    rmdir "$DEMO_CGROUP" 2>/dev/null || true
fi
