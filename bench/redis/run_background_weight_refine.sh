#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="${ROOT:-$(cd "$SCRIPT_DIR/../.." && pwd)}"
OUTROOT="$ROOT/results/reports/background-weight-refine-$(date +%Y%m%d-%H%M%S)"
RUNS="${RUNS:-2}"
BENCH_CLIENTS="${BENCH_CLIENTS:-8}"
BENCH_REQUESTS="${BENCH_REQUESTS:-1500}"
STRESS_WORKERS="${STRESS_WORKERS:-1}"
INTERVAL_MS="${INTERVAL_MS:-1000}"
LATENCY_WEIGHT="${LATENCY_WEIGHT:-1000}"
BATCH_WEIGHT="${BATCH_WEIGHT:-100}"
PSI_THRESHOLD="${PSI_THRESHOLD:-0.05}"
WAIT_THRESHOLD_NS="${WAIT_THRESHOLD_NS:-500000}"
BACKGROUND_RUNTIME_THRESHOLD_NS="${BACKGROUND_RUNTIME_THRESHOLD_NS:-2500000}"
BACKGROUND_WEIGHTS="${BACKGROUND_WEIGHTS:-10 20 30 50}"

mkdir -p "$OUTROOT"

INDEX_CSV="$OUTROOT/index.csv"
INDEX_MD="$OUTROOT/index.md"

printf 'background_weight,cpu_psi_threshold,latency_wait_threshold_ns,background_runtime_threshold_ns,result_dir\n' > "$INDEX_CSV"
printf '# Background Weight 微调\n\n' > "$INDEX_MD"
printf '| background_weight | cpu_psi_threshold | latency_wait_threshold_ns | background_runtime_threshold_ns | result_dir |\n' >> "$INDEX_MD"
printf '| ---: | ---: | ---: | ---: | --- |\n' >> "$INDEX_MD"

for bg in $BACKGROUND_WEIGHTS; do
    printf '[INFO] refine background_weight=%s\n' "$bg"
    LATENCY_WEIGHT="$LATENCY_WEIGHT" \
    BACKGROUND_WEIGHT="$bg" \
    BATCH_WEIGHT="$BATCH_WEIGHT" \
    EULERPILOT_LATENCY_WEIGHT="$LATENCY_WEIGHT" \
    EULERPILOT_BACKGROUND_WEIGHT="$bg" \
    EULERPILOT_BATCH_WEIGHT="$BATCH_WEIGHT" \
    EULERPILOT_CPU_PSI_THRESHOLD="$PSI_THRESHOLD" \
    EULERPILOT_LATENCY_WAIT_THRESHOLD_NS="$WAIT_THRESHOLD_NS" \
    EULERPILOT_BACKGROUND_RUNTIME_THRESHOLD_NS="$BACKGROUND_RUNTIME_THRESHOLD_NS" \
    RUNS="$RUNS" \
    BENCH_CLIENTS="$BENCH_CLIENTS" \
    BENCH_REQUESTS="$BENCH_REQUESTS" \
    STRESS_WORKERS="$STRESS_WORKERS" \
    INTERVAL_MS="$INTERVAL_MS" \
    "$ROOT/bench/redis/run_redis_stress_benchmark.sh"

    latest="$(find "$ROOT/results/reports" -maxdepth 1 -type d -name 'redis-*' | sort | tail -n 1)"
    printf '%s,%s,%s,%s,%s\n' "$bg" "$PSI_THRESHOLD" "$WAIT_THRESHOLD_NS" "$BACKGROUND_RUNTIME_THRESHOLD_NS" "$latest" >> "$INDEX_CSV"
    printf '| %s | %s | %s | %s | %s |\n' "$bg" "$PSI_THRESHOLD" "$WAIT_THRESHOLD_NS" "$BACKGROUND_RUNTIME_THRESHOLD_NS" "$latest" >> "$INDEX_MD"
done

python3 "$ROOT/scripts/summarize_background_weight_refine.py" "$INDEX_CSV" "$OUTROOT/report.md"
printf '[INFO] background weight refine complete: %s\n' "$OUTROOT"
