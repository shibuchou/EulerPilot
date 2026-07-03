#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
HOST="${EULERPILOT_CONSOLE_HOST:-127.0.0.1}"
PORT="${EULERPILOT_CONSOLE_PORT:-18080}"
MODE="foreground"

usage() {
    cat <<USAGE
Usage: web_console/scripts/run_console.sh [--daemon]

Environment:
  EULERPILOT_CONSOLE_HOST   default: 127.0.0.1
  EULERPILOT_CONSOLE_PORT   default: 18080
  EULERPILOT_CONSOLE_TOKEN  required only for non-loopback bind
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --daemon)
            MODE="daemon"
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

cd "$ROOT_DIR/web_console"

if [[ ! -d node_modules ]]; then
    echo "node_modules missing; run: cd $ROOT_DIR/web_console && npm ci" >&2
    exit 1
fi

if [[ ! -f dist/index.html ]]; then
    npm run build
fi

mkdir -p "$ROOT_DIR/web_console/runtime"

export EULERPILOT_CONSOLE_ROOT="$ROOT_DIR"
export EULERPILOT_CONSOLE_HOST="$HOST"
export EULERPILOT_CONSOLE_PORT="$PORT"

cd "$ROOT_DIR"

if [[ "$MODE" == "daemon" ]]; then
    nohup node web_console/backend/src/cli.js > "$ROOT_DIR/web_console/runtime/console.log" 2>&1 &
    START_PID="$!"
    sleep 1
    LISTEN_PID="$(ss -ltnp 2>/dev/null | awk -v port=":$PORT" '$4 ~ port { if (match($0, /pid=[0-9]+/)) { print substr($0, RSTART + 4, RLENGTH - 4); exit } }' || true)"
    echo "${LISTEN_PID:-$START_PID}" > "$ROOT_DIR/web_console/runtime/console.pid"
    echo "EulerPilot Web Console started: http://$HOST:$PORT"
    echo "pid=$(cat "$ROOT_DIR/web_console/runtime/console.pid")"
else
    exec node web_console/backend/src/cli.js
fi
