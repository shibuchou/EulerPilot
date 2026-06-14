#!/usr/bin/env bash
set -euo pipefail

ROOT="/root/EulerPilot"
OUTROOT="$ROOT/results/reports/trigger-sweep-$(date +%Y%m%d-%H%M%S)"
RUNS="${RUNS:-2}"
BENCH_CLIENTS="${BENCH_CLIENTS:-8}"
BENCH_REQUESTS="${BENCH_REQUESTS:-1500}"
STRESS_WORKERS="${STRESS_WORKERS:-1}"
INTERVAL_MS="${INTERVAL_MS:-1000}"
LATENCY_WEIGHT="${LATENCY_WEIGHT:-1000}"
BACKGROUND_WEIGHT="${BACKGROUND_WEIGHT:-5}"
PSI_THRESHOLDS="${PSI_THRESHOLDS:-0.01 0.03 0.05 0.10}"
WAIT_THRESHOLDS_NS="${WAIT_THRESHOLDS_NS:-1000000 3000000 5000000}"
BACKGROUND_RUNTIME_THRESHOLDS_NS="${BACKGROUND_RUNTIME_THRESHOLDS_NS:-2000000 4000000 6000000}"

mkdir -p "$OUTROOT"

INDEX_CSV="$OUTROOT/index.csv"
INDEX_MD="$OUTROOT/index.md"

printf 'cpu_psi_threshold,latency_wait_threshold_ns,background_runtime_threshold_ns,result_dir\n' > "$INDEX_CSV"
printf '# PSI 与等待阈值扫描\n\n' > "$INDEX_MD"
printf '| cpu_psi_threshold | latency_wait_threshold_ns | background_runtime_threshold_ns | result_dir |\n' >> "$INDEX_MD"
printf '| ---: | ---: | ---: | --- |\n' >> "$INDEX_MD"

for psi in $PSI_THRESHOLDS; do
    for wait_ns in $WAIT_THRESHOLDS_NS; do
        for bg_ns in $BACKGROUND_RUNTIME_THRESHOLDS_NS; do
            printf '[INFO] running cpu_psi_threshold=%s latency_wait_threshold_ns=%s background_runtime_threshold_ns=%s\n' "$psi" "$wait_ns" "$bg_ns"
            LATENCY_WEIGHT="$LATENCY_WEIGHT" \
            BACKGROUND_WEIGHT="$BACKGROUND_WEIGHT" \
            EULERPILOT_LATENCY_WEIGHT="$LATENCY_WEIGHT" \
            EULERPILOT_BACKGROUND_WEIGHT="$BACKGROUND_WEIGHT" \
            EULERPILOT_CPU_PSI_THRESHOLD="$psi" \
            EULERPILOT_LATENCY_WAIT_THRESHOLD_NS="$wait_ns" \
            EULERPILOT_BACKGROUND_RUNTIME_THRESHOLD_NS="$bg_ns" \
            RUNS="$RUNS" \
            BENCH_CLIENTS="$BENCH_CLIENTS" \
            BENCH_REQUESTS="$BENCH_REQUESTS" \
            STRESS_WORKERS="$STRESS_WORKERS" \
            INTERVAL_MS="$INTERVAL_MS" \
            "$ROOT/bench/redis/run_redis_stress_benchmark.sh"

            latest="$(find "$ROOT/results/reports" -maxdepth 1 -type d -name 'redis-*' | sort | tail -n 1)"
            printf '%s,%s,%s,%s\n' "$psi" "$wait_ns" "$bg_ns" "$latest" >> "$INDEX_CSV"
            printf '| %s | %s | %s | %s |\n' "$psi" "$wait_ns" "$bg_ns" "$latest" >> "$INDEX_MD"
        done
    done
done

python3 "$ROOT/scripts/summarize_trigger_sweep.py" "$INDEX_CSV" "$OUTROOT/report.md"
printf '[INFO] trigger sweep complete: %s\n' "$OUTROOT"
