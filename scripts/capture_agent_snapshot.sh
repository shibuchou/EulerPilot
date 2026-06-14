#!/usr/bin/env bash
set -euo pipefail

[ $# -ge 2 ] || { printf 'usage: %s <output-file> <interval-ms> [--active] [duration-s] [warmup-cycles] [backend]\n' "$(basename "$0")" >&2; exit 1; }

OUTFILE="$1"
INTERVAL_MS="$2"
MODE="${3:-}"
DURATION_S="${4:-1}"
WARMUP_CYCLES="${5:-0}"
BACKEND="${6:-${EULERPILOT_BACKEND:-cgroup_v2}}"

mkdir -p "$(dirname "$OUTFILE")"
if [ "$MODE" = "--active" ]; then
    /root/EulerPilot/build/eulerpilot-agent \
        --config /root/EulerPilot/configs/agent.yaml \
        --backend "$BACKEND" \
        --interval-ms "$INTERVAL_MS" \
        --duration-s "$DURATION_S" \
        --warmup-cycles "$WARMUP_CYCLES" \
        --active > "$OUTFILE"
else
    /root/EulerPilot/build/eulerpilot-agent \
        --config /root/EulerPilot/configs/agent.yaml \
        --backend "$BACKEND" \
        --interval-ms "$INTERVAL_MS" \
        --duration-s "$DURATION_S" \
        --warmup-cycles "$WARMUP_CYCLES" > "$OUTFILE"
fi
