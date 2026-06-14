#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
make agent
BACKEND="${EULERPILOT_BACKEND:-cgroup_v2}"
if [ "$BACKEND" = "cgroup_v2" ]; then
    ./scripts/setup_cgroup_v2.sh || true
fi
exec ./build/eulerpilot-agent --config configs/agent.yaml --backend "$BACKEND" "$@"
