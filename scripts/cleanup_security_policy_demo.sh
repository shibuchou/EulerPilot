#!/usr/bin/env bash
set -euo pipefail

# Detach any stuck LSM programs (safety net)
bpftool link show 2>/dev/null | grep security_policy_demo | while read -r id rest; do
    bpftool link detach "$id" 2>/dev/null || true
done

# Remove any stray pinned objects
rm -f /sys/fs/bpf/security_policy_demo 2>/dev/null || true
rm -f /sys/fs/bpf/security_policy_demo_link 2>/dev/null || true

echo "security_policy_demo cleanup done"
