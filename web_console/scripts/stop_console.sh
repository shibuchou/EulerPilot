#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PID_FILE="$ROOT_DIR/web_console/runtime/console.pid"
PORT="${EULERPILOT_CONSOLE_PORT:-18080}"

if [[ ! -f "$PID_FILE" ]]; then
    echo "console pid file not found: $PID_FILE"
else
    PID="$(cat "$PID_FILE")"
    if [[ -n "$PID" ]] && kill -0 "$PID" 2>/dev/null; then
        kill "$PID"
        echo "stopped EulerPilot Web Console pid=$PID"
    else
        echo "EulerPilot Web Console pid=$PID is not running"
    fi
    rm -f "$PID_FILE"
fi

LISTEN_PIDS="$(ss -ltnp 2>/dev/null | awk -v port=":$PORT" '$4 ~ port { while (match($0, /pid=[0-9]+/)) { print substr($0, RSTART + 4, RLENGTH - 4); $0 = substr($0, RSTART + RLENGTH) } }' | sort -u || true)"
for LISTEN_PID in $LISTEN_PIDS; do
    if kill -0 "$LISTEN_PID" 2>/dev/null; then
        kill "$LISTEN_PID"
        echo "stopped EulerPilot Web Console listener pid=$LISTEN_PID"
    fi
done
