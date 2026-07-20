#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="${ROOT:-$(cd "$SCRIPT_DIR/../.." && pwd)}"
OUTROOT="$ROOT/results/reports/profile-sweep-$(date +%Y%m%d-%H%M%S)"
RUNS="${RUNS:-2}"
BENCH_CLIENTS="${BENCH_CLIENTS:-8}"
BENCH_REQUESTS="${BENCH_REQUESTS:-1500}"
STRESS_WORKERS="${STRESS_WORKERS:-1}"
INTERVAL_MS="${INTERVAL_MS:-1000}"

mkdir -p "$OUTROOT"

CONFIGS=(
  "1000 20"
  "1000 10"
  "1000 5"
  "800 20"
)

printf '# Redis Profile Sweep\n\n' > "$OUTROOT/index.md"
printf '| latency_weight | background_weight | result_dir |\n' >> "$OUTROOT/index.md"
printf '| ---: | ---: | --- |\n' >> "$OUTROOT/index.md"

for cfg in "${CONFIGS[@]}"; do
    read -r LAT BG <<< "$cfg"
    printf '[INFO] running config latency=%s background=%s\n' "$LAT" "$BG"
    LATENCY_WEIGHT="$LAT" \
    BACKGROUND_WEIGHT="$BG" \
    EULERPILOT_LATENCY_WEIGHT="$LAT" \
    EULERPILOT_BACKGROUND_WEIGHT="$BG" \
    RUNS="$RUNS" \
    BENCH_CLIENTS="$BENCH_CLIENTS" \
    BENCH_REQUESTS="$BENCH_REQUESTS" \
    STRESS_WORKERS="$STRESS_WORKERS" \
    INTERVAL_MS="$INTERVAL_MS" \
    "$ROOT/bench/redis/run_redis_stress_benchmark.sh"

    latest="$(find "$ROOT/results/reports" -maxdepth 1 -type d -name 'redis-*' | sort | tail -n 1)"
    printf '| %s | %s | %s |\n' "$LAT" "$BG" "$latest" >> "$OUTROOT/index.md"
done

printf '[INFO] profile sweep complete: %s\n' "$OUTROOT"
