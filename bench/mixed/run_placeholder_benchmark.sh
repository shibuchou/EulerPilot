#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="${ROOT:-$(cd "$SCRIPT_DIR/../.." && pwd)}"
RESULT_ROOT="$ROOT/results"
STAMP="$(date +%Y%m%d-%H%M%S)"
OUTDIR="$RESULT_ROOT/reports/mixed-$STAMP"

FRONT_CMD="${FRONT_CMD:-sleep 5}"
BACK_CMD="${BACK_CMD:-yes >/dev/null}"
INTERVAL_MS="${INTERVAL_MS:-1000}"

mkdir -p "$OUTDIR"

printf '[INFO] benchmark output: %s\n' "$OUTDIR"
printf '[INFO] front command: %s\n' "$FRONT_CMD"
printf '[INFO] back command: %s\n' "$BACK_CMD"

"$ROOT/scripts/collect_system_snapshot.sh" "$OUTDIR"

bash -lc "$BACK_CMD" &
BACK_PID=$!
bash -lc "$FRONT_CMD" &
FRONT_PID=$!

sleep 1
"$ROOT/scripts/capture_agent_snapshot.sh" "$OUTDIR/agent_snapshot.txt" "$INTERVAL_MS"

printf 'front_pid=%s\n' "$FRONT_PID" > "$OUTDIR/pids.env"
printf 'back_pid=%s\n' "$BACK_PID" >> "$OUTDIR/pids.env"

wait "$FRONT_PID" 2>/dev/null || true
kill "$BACK_PID" 2>/dev/null || true
wait "$BACK_PID" 2>/dev/null || true

cat > "$OUTDIR/summary.md" <<EOF
# Mixed Placeholder Benchmark

- timestamp: $(date --iso-8601=seconds)
- front command: \`$FRONT_CMD\`
- back command: \`$BACK_CMD\`
- agent snapshot: \`agent_snapshot.txt\`
- system snapshot: collected

This run is a benchmark framework validation run, not yet a final Redis + stress-ng experiment.
EOF

printf '[INFO] benchmark framework validation complete.\n'
