#!/usr/bin/env bash
set -euo pipefail

ROOT="/sys/fs/cgroup/eulerpilot"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SETUP_SCRIPT="${SETUP_SCRIPT:-$SCRIPT_DIR/setup_cgroup_v2.sh}"

usage() {
    printf 'usage: %s <latency|batch|background> <pid>\n' "$(basename "$0")" >&2
    exit 1
}

[ $# -eq 2 ] || usage

group="$1"
pid="$2"

case "$group" in
    latency|batch|background) ;;
    *) usage ;;
esac

[ -d "$ROOT/$group" ] || "$SETUP_SCRIPT"
[ -d "/proc/$pid" ] || { printf '[FAIL] pid %s does not exist\n' "$pid" >&2; exit 1; }

echo "$pid" > "$ROOT/$group/cgroup.procs"
printf '[INFO] assigned pid=%s to %s\n' "$pid" "$ROOT/$group"
