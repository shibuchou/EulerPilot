#!/usr/bin/env bash
set -euo pipefail

# Detach any stuck LSM programs (safety net). With pipefail enabled, a
# no-match grep would make a clean system look like a cleanup failure, so keep
# the "no residue" path explicitly successful.
LINK_IDS="$(bpftool link show 2>/dev/null | awk '/security_policy_demo/ { sub(/:$/, "", $1); print $1 }' || true)"
for id in $LINK_IDS; do
    bpftool link detach "$id" 2>/dev/null || true
done

# Remove any stray pinned objects
rm -f /sys/fs/bpf/security_policy_demo 2>/dev/null || true
rm -f /sys/fs/bpf/security_policy_demo_link 2>/dev/null || true

echo "security_policy_demo cleanup done"
