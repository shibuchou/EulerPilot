#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="${ROOT:-$(cd "$SCRIPT_DIR/../.." && pwd)}"
OUTROOT="$ROOT/results/reports/psi-threshold-sweep-$(date +%Y%m%d-%H%M%S)"
RUNS="${RUNS:-2}"
BENCH_CLIENTS="${BENCH_CLIENTS:-8}"
BENCH_REQUESTS="${BENCH_REQUESTS:-1500}"
STRESS_WORKERS="${STRESS_WORKERS:-1}"
INTERVAL_MS="${INTERVAL_MS:-1000}"
LATENCY_WEIGHT="${LATENCY_WEIGHT:-1000}"
BACKGROUND_WEIGHT="${BACKGROUND_WEIGHT:-5}"

mkdir -p "$OUTROOT"

THRESHOLDS=(
  "0.01"
  "0.03"
  "0.05"
  "0.10"
)

printf '# PSI Threshold Sweep\n\n' > "$OUTROOT/index.md"
printf '| cpu_psi_threshold | latency_weight | background_weight | result_dir |\n' >> "$OUTROOT/index.md"
printf '| ---: | ---: | ---: | --- |\n' >> "$OUTROOT/index.md"

for threshold in "${THRESHOLDS[@]}"; do
    printf '[INFO] running threshold=%s\n' "$threshold"
    LATENCY_WEIGHT="$LATENCY_WEIGHT" \
    BACKGROUND_WEIGHT="$BACKGROUND_WEIGHT" \
    EULERPILOT_LATENCY_WEIGHT="$LATENCY_WEIGHT" \
    EULERPILOT_BACKGROUND_WEIGHT="$BACKGROUND_WEIGHT" \
    EULERPILOT_CPU_PSI_THRESHOLD="$threshold" \
    RUNS="$RUNS" \
    BENCH_CLIENTS="$BENCH_CLIENTS" \
    BENCH_REQUESTS="$BENCH_REQUESTS" \
    STRESS_WORKERS="$STRESS_WORKERS" \
    INTERVAL_MS="$INTERVAL_MS" \
    "$ROOT/bench/redis/run_redis_stress_benchmark.sh"

    latest="$(find "$ROOT/results/reports" -maxdepth 1 -type d -name 'redis-*' | sort | tail -n 1)"
    printf '| %s | %s | %s | %s |\n' "$threshold" "$LATENCY_WEIGHT" "$BACKGROUND_WEIGHT" "$latest" >> "$OUTROOT/index.md"
done

printf '[INFO] psi threshold sweep complete: %s\n' "$OUTROOT"
