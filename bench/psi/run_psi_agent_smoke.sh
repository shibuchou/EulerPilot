#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="${ROOT:-$(cd "$SCRIPT_DIR/../.." && pwd)}"
SCX_BIN="${SCX_BIN:-$(command -v scx_eulerpilot 2>/dev/null || true)}"
SCX_BIN="${SCX_BIN:-/usr/local/bin/scx_eulerpilot}"
LEGACY_SCX_BIN="${LEGACY_SCX_BIN:-/root/olk/kernel-OLK-6.6-atomgit/tools/sched_ext/build/bin/scx_eulerpilot}"
if [ ! -x "$SCX_BIN" ] && [ "${ALLOW_LEGACY_SCX_FALLBACK:-0}" = "1" ] && [ -x "$LEGACY_SCX_BIN" ]; then
    printf '[WARN] using legacy scx fallback: %s\n' "$LEGACY_SCX_BIN" >&2
    SCX_BIN="$LEGACY_SCX_BIN"
fi
MODE="${MODE:?MODE is required (redis_only|redis_stress|redis_recover|redis_repeat3)}"
OUTDIR="$ROOT/results/smoke/psi-${MODE}-$(date +%Y%m%d-%H%M%S)"
REDIS_PORT="${REDIS_PORT:-6396}"
INTERVAL_MS="${INTERVAL_MS:-1000}"
AGENT_DURATION="${AGENT_DURATION:-12}"
STRESS_WORKERS="${STRESS_WORKERS:-2}"

mkdir -p "$OUTDIR"

cleanup() {
    kill "${AGENT_PID:-0}" 2>/dev/null || true
    kill "${STRESS_PID:-0}" 2>/dev/null || true
    pkill -f 'redis-benchmark' 2>/dev/null || true
    pkill -f '(^|/)scx_' 2>/dev/null || true
    redis-cli -p "$REDIS_PORT" shutdown nosave >/dev/null 2>&1 || true
    "$ROOT/scripts/rollback.sh" >/dev/null 2>&1 || true
}
trap cleanup EXIT

BENCHMARK_COMMAND="./build/eulerpilot-agent --backend sched_ext --gate-mode psi"
STRESS_COMMAND="stress-ng --cpu $STRESS_WORKERS"
BACKEND="sched_ext" EULERPILOT_GATE_MODE="psi" python3 "$ROOT/scripts/write_run_manifest.py" "$OUTDIR/run_manifest.json" "$MODE"

"$ROOT/scripts/rollback.sh" > "$OUTDIR/reset.log" 2>&1 || true
rm -f /tmp/eulerpilot-psi-gate-trace.jsonl /tmp/eulerpilot-scx-session.log
pkill -f 'redis-benchmark' 2>/dev/null || true
pkill -f '(^|/)scx_' 2>/dev/null || true
redis-server --port "$REDIS_PORT" --save "" --appendonly no --daemonize yes

run_agent() {
    EULERPILOT_GATE_MODE=psi \
    EULERPILOT_SCX_BINARY="$SCX_BIN" \
    EULERPILOT_CPU_PSI_THRESHOLD="${EULERPILOT_CPU_PSI_THRESHOLD:-0.0}" \
    EULERPILOT_LATENCY_WAIT_THRESHOLD_NS="${EULERPILOT_LATENCY_WAIT_THRESHOLD_NS:-1}" \
    EULERPILOT_BACKGROUND_RUNTIME_THRESHOLD_NS="${EULERPILOT_BACKGROUND_RUNTIME_THRESHOLD_NS:-1}" \
    "$ROOT/build/eulerpilot-agent" --backend sched_ext --active --duration-s "$AGENT_DURATION" --interval-ms "$INTERVAL_MS" \
        > "$OUTDIR/agent.log" 2>&1 &
    AGENT_PID=$!
}

wait_for_agent() {
    wait "$AGENT_PID" || true
}

write_invalid_reason() {
    local reason="$1"
    printf '%s\n' "$reason" > "$OUTDIR/invalid_smoke_reason.txt"
}

count_process_comm() {
    local target="$1"
    ps -eo comm= 2>/dev/null | awk -v target="$target" '$1 == target { count++ } END { print count + 0 }'
}

preflight_assert() {
    local expected_background="$1"
    local input_file="$2"
    local background_count
    local latency_count

    background_count=$(awk -F= '/gate_relevant_background_count/ {print $2}' "$input_file" 2>/dev/null || echo 0)
    latency_count=$(awk -F= '/gate_relevant_latency_count/ {print $2}' "$input_file" 2>/dev/null || echo 0)

    if [ "$latency_count" -lt 1 ]; then
        write_invalid_reason "missing-gate-relevant-latency-workload"
        return 1
    fi

    if [ "$expected_background" = "0" ] && [ "$background_count" -ne 0 ]; then
        write_invalid_reason "unexpected-gate-relevant-background-workload"
        return 1
    fi

    if [ "$expected_background" = "1" ] && [ "$background_count" -lt 1 ]; then
        write_invalid_reason "missing-gate-relevant-background-workload"
        return 1
    fi

    return 0
}

case "$MODE" in
    redis_only)
        run_agent
        wait_for_agent
        "$ROOT/scripts/capture_gate_runtime.sh" "$OUTDIR" "phase1"
        preflight_assert 0 "$OUTDIR/phase1_preflight.txt" || true
        ;;
    redis_stress)
        run_agent
        stress-ng --cpu "$STRESS_WORKERS" --timeout 6 > "$OUTDIR/stress.log" 2>&1 &
        STRESS_PID=$!
        wait "$STRESS_PID" || true
        wait_for_agent
        "$ROOT/scripts/capture_gate_runtime.sh" "$OUTDIR" "phase_active"
        preflight_assert 1 "$OUTDIR/phase_active_preflight.txt" || true
        ;;
    redis_recover)
        run_agent
        stress-ng --cpu "$STRESS_WORKERS" --timeout 6 > "$OUTDIR/stress.log" 2>&1 &
        STRESS_PID=$!
        sleep 4
        "$ROOT/scripts/capture_gate_runtime.sh" "$OUTDIR" "phase_active"
        wait "$STRESS_PID" || true
        sleep 4
        "$ROOT/scripts/capture_gate_runtime.sh" "$OUTDIR" "phase_recover"
        wait_for_agent
        preflight_assert 1 "$OUTDIR/phase_active_preflight.txt" || true
        ;;
    redis_repeat3)
        AGENT_DURATION="${AGENT_DURATION:-24}"
        run_agent
        for idx in 1 2 3; do
            stress-ng --cpu "$STRESS_WORKERS" --timeout 5 > "$OUTDIR/stress_${idx}.log" 2>&1 &
            STRESS_PID=$!
            sleep 4
            "$ROOT/scripts/capture_gate_runtime.sh" "$OUTDIR" "phase_${idx}_active"
            preflight_assert 1 "$OUTDIR/phase_${idx}_active_preflight.txt" || true
            wait "$STRESS_PID" || true
            sleep 4
            "$ROOT/scripts/capture_gate_runtime.sh" "$OUTDIR" "phase_${idx}_recover"
        done
        wait_for_agent
        ;;
    *)
        printf 'unknown mode: %s\n' "$MODE" >&2
        exit 1
        ;;
esac

"$ROOT/scripts/capture_gate_runtime.sh" "$OUTDIR" "final"
printf '[INFO] psi agent smoke output: %s\n' "$OUTDIR"
