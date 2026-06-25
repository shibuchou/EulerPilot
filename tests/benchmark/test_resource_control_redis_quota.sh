#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

CGROUP_ROOT="/sys/fs/cgroup/eulerpilot"
BG="$CGROUP_ROOT/background"
REDIS_PORT="${REDIS_PORT:-16379}"
BENCH_CLIENTS="${BENCH_CLIENTS:-32}"
BENCH_REQUESTS="${BENCH_REQUESTS:-30000}"
HOG_WORKERS="${HOG_WORKERS:-4}"
RESULT_DIR="${RESULT_DIR:-results/resource_control/redis-quota-$(date +%Y%m%d-%H%M%S)}"
AGENT_PID=""
REDIS_PID=""
HOG_PIDS=()

fail() {
    printf '[FAIL] %s\n' "$*" >&2
    exit 1
}

info() {
    printf '[INFO] %s\n' "$*" | tee -a "$RESULT_DIR/benchmark.log"
}

cleanup_hogs() {
    local pid
    for pid in "${HOG_PIDS[@]:-}"; do
        if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
            kill "$pid" 2>/dev/null || true
            wait "$pid" 2>/dev/null || true
        fi
    done
    HOG_PIDS=()
}

cleanup() {
    set +e
    cleanup_hogs
    if [ -n "$AGENT_PID" ] && kill -0 "$AGENT_PID" 2>/dev/null; then
        kill "$AGENT_PID" 2>/dev/null
        wait "$AGENT_PID" 2>/dev/null
    fi
    if [ -n "$REDIS_PID" ] && kill -0 "$REDIS_PID" 2>/dev/null; then
        redis-cli -h 127.0.0.1 -p "$REDIS_PORT" shutdown nosave >/dev/null 2>&1 || true
        kill "$REDIS_PID" 2>/dev/null || true
        wait "$REDIS_PID" 2>/dev/null || true
    fi
    scripts/rollback.sh > "$RESULT_DIR/rollback.log" 2>&1
}
trap cleanup EXIT

cpu_stat_value() {
    local key="$1"
    awk -v key="$key" '$1 == key { print $2; found=1 } END { if (!found) print 0 }' "$BG/cpu.stat"
}

wait_for_value() {
    local file="$1"
    local expected="$2"
    local deadline=$((SECONDS + 20))
    while [ "$SECONDS" -lt "$deadline" ]; do
        if [ -f "$file" ] && [ "$(cat "$file")" = "$expected" ]; then
            return 0
        fi
        sleep 0.2
    done
    printf '[DEBUG] %s now=%s expected=%s\n' \
        "$file" "$(cat "$file" 2>/dev/null || true)" "$expected" >&2
    return 1
}

start_redis() {
    local redis_dir="$ROOT/$RESULT_DIR/redis"
    mkdir -p "$redis_dir"
    cat > "$redis_dir/redis.conf" <<EOF_REDIS
bind 127.0.0.1
port $REDIS_PORT
save ""
appendonly no
daemonize no
protected-mode no
dir $redis_dir
logfile $redis_dir/redis.log
EOF_REDIS

    redis-server "$redis_dir/redis.conf" > /dev/null 2>&1 &
    REDIS_PID="$!"
    for _ in $(seq 1 50); do
        if redis-cli -h 127.0.0.1 -p "$REDIS_PORT" ping >/dev/null 2>&1; then
            info "redis ready pid=$REDIS_PID port=$REDIS_PORT"
            return 0
        fi
        sleep 0.1
    done
    fail "redis did not become ready on port $REDIS_PORT"
}

start_background_hogs() {
    local workers="$1"
    local i pid
    HOG_PIDS=()
    for i in $(seq 1 "$workers"); do
        yes > /dev/null &
        pid="$!"
        echo "$pid" > "$BG/cgroup.procs"
        HOG_PIDS+=("$pid")
    done
    info "background hogs=${HOG_PIDS[*]}"
}

write_agent_config() {
    cat > "$RESULT_DIR/agent.redis-quota.yaml" <<'YAML'
agent:
  name: EulerPilot
  mode: active
skills_config_path: skills.redis-quota.yaml
scheduler:
  type: cgroup_v2
exporter:
  prometheus:
    enabled: false
YAML

    cat > "$RESULT_DIR/skills.redis-quota.yaml" <<'YAML'
schema_version: 2
skills:
- name: resource_control
  kind: runtime
  enabled: true
  config:
    mode: enforce
    controllers:
      cpu:
        max:
          enabled: true
      memory:
        enabled: false
      io:
        enabled: false
        weight:
          enabled: false
        max:
          enabled: false
    profiles:
      background:
        normal:
          cpu_max: max
        pressure:
          cpu_max: '10000 100000'
- name: psi_gate
  kind: runtime
  enabled: true
  config: {}
YAML
}

run_redis_benchmark() {
    local label="$1"
    redis-benchmark -h 127.0.0.1 -p "$REDIS_PORT" \
        -c "$BENCH_CLIENTS" -n "$BENCH_REQUESTS" \
        -t get,set --csv > "$RESULT_DIR/$label.redis.csv"
    awk -f scripts/extract_redis_metrics.awk \
        "$RESULT_DIR/$label.redis.csv" > "$RESULT_DIR/$label.summary.csv"
}

measure_phase() {
    local label="$1"
    local usage_before periods_before throttled_before throttled_usec_before
    local usage_after periods_after throttled_after throttled_usec_after
    local start_ns end_ns

    usage_before="$(cpu_stat_value usage_usec)"
    periods_before="$(cpu_stat_value nr_periods)"
    throttled_before="$(cpu_stat_value nr_throttled)"
    throttled_usec_before="$(cpu_stat_value throttled_usec)"
    start_ns="$(date +%s%N)"

    run_redis_benchmark "$label"

    end_ns="$(date +%s%N)"
    usage_after="$(cpu_stat_value usage_usec)"
    periods_after="$(cpu_stat_value nr_periods)"
    throttled_after="$(cpu_stat_value nr_throttled)"
    throttled_usec_after="$(cpu_stat_value throttled_usec)"

    local duration_ms=$(((end_ns - start_ns) / 1000000))
    {
        printf '%s_duration_ms=%s\n' "$label" "$duration_ms"
        printf '%s_usage_delta_usec=%s\n' "$label" "$((usage_after - usage_before))"
        printf '%s_nr_periods_delta=%s\n' "$label" "$((periods_after - periods_before))"
        printf '%s_nr_throttled_delta=%s\n' "$label" "$((throttled_after - throttled_before))"
        printf '%s_throttled_usec_delta=%s\n' "$label" "$((throttled_usec_after - throttled_usec_before))"
    } >> "$RESULT_DIR/measurements.env"
}

extract_rps() {
    local summary="$1"
    local test_name="$2"
    awk -F, -v test="$test_name" '$1 == test { print $2; found=1 } END { if (!found) print 0 }' "$summary"
}

write_summary() {
    # shellcheck disable=SC1090
    . "$RESULT_DIR/measurements.env"

    local default_get default_set limited_get limited_set
    default_get="$(extract_rps "$RESULT_DIR/default_noisy.summary.csv" GET)"
    default_set="$(extract_rps "$RESULT_DIR/default_noisy.summary.csv" SET)"
    limited_get="$(extract_rps "$RESULT_DIR/eulerpilot_quota.summary.csv" GET)"
    limited_set="$(extract_rps "$RESULT_DIR/eulerpilot_quota.summary.csv" SET)"

    read -r BG_RATE_DEFAULT BG_RATE_LIMITED BG_RATE_RATIO GET_RATIO SET_RATIO <<EOF_RATES
$(python3 - "$default_noisy_usage_delta_usec" "$default_noisy_duration_ms" \
           "$eulerpilot_quota_usage_delta_usec" "$eulerpilot_quota_duration_ms" \
           "$default_get" "$limited_get" "$default_set" "$limited_set" <<'PY'
import sys
du, dd, lu, ld, dg, lg, ds, ls = map(float, sys.argv[1:])
default_rate = du / (dd / 1000.0) if dd > 0 else 0.0
limited_rate = lu / (ld / 1000.0) if ld > 0 else 0.0
bg_ratio = limited_rate / default_rate if default_rate > 0 else 0.0
get_ratio = lg / dg if dg > 0 else 0.0
set_ratio = ls / ds if ds > 0 else 0.0
print(f"{default_rate:.2f} {limited_rate:.2f} {bg_ratio:.4f} {get_ratio:.4f} {set_ratio:.4f}")
PY
)
EOF_RATES

    python3 - "$BG_RATE_RATIO" "$eulerpilot_quota_nr_throttled_delta" \
        "$eulerpilot_quota_throttled_usec_delta" "$limited_get" "$limited_set" <<'PY'
import sys
bg_ratio = float(sys.argv[1])
throttled = int(sys.argv[2])
throttled_usec = int(sys.argv[3])
limited_get = float(sys.argv[4])
limited_set = float(sys.argv[5])
if bg_ratio >= 0.70:
    raise SystemExit(f"background CPU usage ratio too high: {bg_ratio:.4f}")
if throttled <= 0:
    raise SystemExit("nr_throttled did not increase under Redis quota benchmark")
if throttled_usec <= 0:
    raise SystemExit("throttled_usec did not increase under Redis quota benchmark")
if limited_get <= 0 or limited_set <= 0:
    raise SystemExit("Redis benchmark did not produce positive GET/SET RPS")
PY

    cat > "$RESULT_DIR/summary.txt" <<EOF_SUMMARY
result=pass
benchmark=redis_background_cpu_quota
redis_port=$REDIS_PORT
bench_clients=$BENCH_CLIENTS
bench_requests=$BENCH_REQUESTS
hog_workers=$HOG_WORKERS
cpu_max_pressure=10000 100000
default_get_rps=$default_get
default_set_rps=$default_set
limited_get_rps=$limited_get
limited_set_rps=$limited_set
get_rps_ratio=$GET_RATIO
set_rps_ratio=$SET_RATIO
default_background_usage_rate_usec_per_s=$BG_RATE_DEFAULT
limited_background_usage_rate_usec_per_s=$BG_RATE_LIMITED
background_usage_rate_ratio=$BG_RATE_RATIO
default_nr_throttled_delta=$default_noisy_nr_throttled_delta
limited_nr_throttled_delta=$eulerpilot_quota_nr_throttled_delta
default_throttled_usec_delta=$default_noisy_throttled_usec_delta
limited_throttled_usec_delta=$eulerpilot_quota_throttled_usec_delta
EOF_SUMMARY

    cat > "$RESULT_DIR/report.md" <<EOF_REPORT
# Redis + Background CPU Quota Benchmark

- result: pass
- Redis port: \`$REDIS_PORT\`
- clients: \`$BENCH_CLIENTS\`
- requests per phase: \`$BENCH_REQUESTS\`
- background workers: \`$HOG_WORKERS\`
- Resource Control pressure quota: \`cpu.max=10000 100000\`

## Key Results

| Metric | Default Noisy | EulerPilot Quota |
|--------|---------------|------------------|
| GET RPS | $default_get | $limited_get |
| SET RPS | $default_set | $limited_set |
| Background CPU usec/s | $BG_RATE_DEFAULT | $BG_RATE_LIMITED |
| nr_throttled delta | $default_noisy_nr_throttled_delta | $eulerpilot_quota_nr_throttled_delta |
| throttled_usec delta | $default_noisy_throttled_usec_delta | $eulerpilot_quota_throttled_usec_delta |

## Interpretation

This benchmark compares an unmanaged background CPU noisy workload with the same workload under EulerPilot Resource Control. The pass condition focuses on the control effect: the background cgroup CPU usage rate must drop and cgroup v2 throttling counters must increase under \`cpu.max\`. Redis throughput is reported as workload evidence, but this result is not claimed as a Redis performance improvement; profile tuning for foreground benefit remains a follow-up benchmark task.

## Artifacts

- \`default_noisy.redis.csv\`
- \`default_noisy.summary.csv\`
- \`eulerpilot_quota.redis.csv\`
- \`eulerpilot_quota.summary.csv\`
- \`resource_control_events.jsonl\`
- \`agent.log\`
- \`summary.txt\`
EOF_REPORT
}

[ "$(id -u)" -eq 0 ] || fail 'resource_control redis quota benchmark must run as root'
mkdir -p "$RESULT_DIR"
: > "$RESULT_DIR/benchmark.log"
: > "$RESULT_DIR/measurements.env"

command -v redis-server >/dev/null 2>&1 || fail 'redis-server is required'
command -v redis-benchmark >/dev/null 2>&1 || fail 'redis-benchmark is required'

make agent
scripts/setup_cgroup_v2.sh > "$RESULT_DIR/setup.log" 2>&1
mkdir -p reports/events run/eulerpilot
: > reports/events/resource_control.jsonl
: > run/eulerpilot/action_journal.jsonl

[ -w "$BG/cpu.max" ] || fail "$BG/cpu.max is not writable"
[ -f "$BG/cpu.stat" ] || fail "$BG/cpu.stat is missing"

write_agent_config
start_redis

info "phase=default_noisy"
echo max > "$BG/cpu.max"
start_background_hogs "$HOG_WORKERS"
sleep 1
measure_phase default_noisy
cleanup_hogs

info "phase=eulerpilot_quota"
scripts/setup_cgroup_v2.sh > "$RESULT_DIR/setup-before-quota.log" 2>&1
start_background_hogs "$HOG_WORKERS"
timeout 40s ./build/eulerpilot-agent \
    --config "$RESULT_DIR/agent.redis-quota.yaml" \
    --backend cgroup_v2 \
    --gate-mode always-active \
    --active \
    --duration-s 24 \
    --interval-ms 500 \
    --jsonl \
    > "$RESULT_DIR/agent.log" 2>&1 &
AGENT_PID="$!"

wait_for_value "$BG/cpu.max" '10000 100000' || fail 'cpu.max pressure value was not applied'
measure_phase eulerpilot_quota
wait "$AGENT_PID"
AGENT_PID=""
cleanup_hogs

grep -q '"file":"cpu.max"' reports/events/resource_control.jsonl ||
    fail 'resource_control audit log missing cpu.max event'
grep -q '"result":"applied"' reports/events/resource_control.jsonl ||
    fail 'resource_control audit log missing applied result'
grep -q '"result":"restored"' reports/events/resource_control.jsonl ||
    fail 'resource_control audit log missing restored result'

cp reports/events/resource_control.jsonl "$RESULT_DIR/resource_control_events.jsonl"
write_summary

info "resource_control Redis quota benchmark result saved to $RESULT_DIR"
