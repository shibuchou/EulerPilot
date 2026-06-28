#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

CGROUP_ROOT="/sys/fs/cgroup/eulerpilot"
LAT="$CGROUP_ROOT/latency"
BG="$CGROUP_ROOT/background"
REDIS_PORT="${REDIS_PORT:-16383}"
NGINX_PORT="${NGINX_PORT:-18085}"
REDIS_BENCH_CLIENTS="${REDIS_BENCH_CLIENTS:-32}"
REDIS_BENCH_REQUESTS="${REDIS_BENCH_REQUESTS:-200000}"
WRK_THREADS="${WRK_THREADS:-2}"
WRK_CONNECTIONS="${WRK_CONNECTIONS:-32}"
WRK_DURATION="${WRK_DURATION:-10s}"
HOG_WORKERS="${HOG_WORKERS:-4}"
AGENT_DURATION_S="${AGENT_DURATION_S:-34}"
AGENT_INTERVAL_MS="${AGENT_INTERVAL_MS:-500}"
BUSINESS_RETENTION_MIN="${BUSINESS_RETENTION_MIN:-0.70}"
RESULT_DIR="${RESULT_DIR:-results/resource_control/mixed-multi-resource-$(date +%Y%m%d-%H%M%S)}"
REDIS_DIR="$ROOT/$RESULT_DIR/redis"
NGINX_DIR="$ROOT/$RESULT_DIR/nginx"
NGINX_CONF="$NGINX_DIR/nginx.conf"
AGENT_PID=""
REDIS_PID=""
NGINX_PID=""
HOG_PIDS=()

PHASE_LABELS=(cpu_cpuset_no_quota cpu_cpuset_quota50 multi_quota50 multi_quota20)
PHASE_CPU_MAX=("max" "50000 100000" "50000 100000" "20000 100000")
PHASE_MEMORY_ENABLED=("false" "false" "true" "true")
PHASE_LATENCY_MEMORY_LOW=("0" "0" "67108864" "67108864")
PHASE_BACKGROUND_MEMORY_HIGH=("max" "max" "134217728" "134217728")

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

cleanup_agent() {
    if [ -n "$AGENT_PID" ] && kill -0 "$AGENT_PID" 2>/dev/null; then
        kill "$AGENT_PID" 2>/dev/null || true
        wait "$AGENT_PID" 2>/dev/null || true
    fi
    AGENT_PID=""
}

cleanup_redis() {
    if [ -n "$REDIS_PID" ] && kill -0 "$REDIS_PID" 2>/dev/null; then
        redis-cli -h 127.0.0.1 -p "$REDIS_PORT" shutdown nosave >/dev/null 2>&1 || true
        kill "$REDIS_PID" 2>/dev/null || true
        wait "$REDIS_PID" 2>/dev/null || true
    fi
    REDIS_PID=""
}

cleanup_nginx() {
    if [ -f "$NGINX_DIR/nginx.pid" ]; then
        nginx -p "$NGINX_DIR" -c "$NGINX_CONF" -s quit >/dev/null 2>&1 || true
        kill "$(cat "$NGINX_DIR/nginx.pid" 2>/dev/null || true)" 2>/dev/null || true
    fi
    if [ -n "$NGINX_PID" ] && kill -0 "$NGINX_PID" 2>/dev/null; then
        kill "$NGINX_PID" 2>/dev/null || true
        wait "$NGINX_PID" 2>/dev/null || true
    fi
    NGINX_PID=""
}

cleanup() {
    set +e
    cleanup_hogs
    cleanup_agent
    cleanup_redis
    cleanup_nginx
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

wait_for_nginx() {
    local deadline=$((SECONDS + 20))
    while [ "$SECONDS" -lt "$deadline" ]; do
        if curl -fsS "http://127.0.0.1:$NGINX_PORT/" >/dev/null 2>&1; then
            return 0
        fi
        sleep 0.2
    done
    return 1
}

start_redis() {
    mkdir -p "$REDIS_DIR"
    cat > "$REDIS_DIR/redis.conf" <<EOF_REDIS
bind 127.0.0.1
port $REDIS_PORT
save ""
appendonly no
daemonize no
protected-mode no
dir $REDIS_DIR
logfile $REDIS_DIR/redis.log
EOF_REDIS

    redis-server "$REDIS_DIR/redis.conf" > /dev/null 2>&1 &
    REDIS_PID="$!"
    for _ in $(seq 1 80); do
        if redis-cli -h 127.0.0.1 -p "$REDIS_PORT" ping >/dev/null 2>&1; then
            info "redis ready pid=$REDIS_PID port=$REDIS_PORT"
            return 0
        fi
        sleep 0.1
    done
    fail "redis did not become ready on port $REDIS_PORT"
}

start_nginx() {
    mkdir -p "$NGINX_DIR"
    cat > "$NGINX_CONF" <<EOF_NGINX
daemon off;
worker_processes 1;
error_log $NGINX_DIR/error.log;
pid $NGINX_DIR/nginx.pid;

events {
    worker_connections 2048;
}

http {
    access_log off;
    server {
        listen $NGINX_PORT;
        location / {
            return 200 'EulerPilot mixed multi-resource profile benchmark\n';
        }
    }
}
EOF_NGINX

    nginx -p "$NGINX_DIR" -c "$NGINX_CONF" > "$RESULT_DIR/nginx.stdout.log" 2>&1 &
    NGINX_PID="$!"
    wait_for_nginx || fail "nginx did not become ready on port $NGINX_PORT"
    info "nginx ready pid=$NGINX_PID port=$NGINX_PORT"
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
    local label="$1"
    local background_cpu_max="$2"
    local memory_enabled="$3"
    local latency_memory_low="$4"
    local background_memory_high="$5"

    cat > "$RESULT_DIR/agent.$label.yaml" <<YAML
agent:
  name: EulerPilot
  mode: active
skills_config_path: skills.$label.yaml
scheduler:
  type: cgroup_v2
exporter:
  prometheus:
    enabled: false
YAML

    cat > "$RESULT_DIR/skills.$label.yaml" <<YAML
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
        enabled: $memory_enabled
        high:
          enabled: true
        low:
          enabled: true
        max:
          enabled: false
        reclaim:
          enabled: false
      io:
        enabled: false
        weight:
          enabled: false
        max:
          enabled: false
    profiles:
      latency:
        cpu_max: max
        memory_low: '$latency_memory_low'
        memory_high: max
        memory_max: max
      batch:
        cpu_max: max
        memory_low: '0'
        memory_high: max
        memory_max: max
      background:
        normal:
          cpu_max: max
          memory_high: max
        pressure:
          cpu_max: '$background_cpu_max'
          memory_low: '0'
          memory_high: '$background_memory_high'
          memory_max: max
- name: psi_gate
  kind: runtime
  enabled: true
  config: {}
YAML
}

run_mixed_benchmark() {
    local label="$1"
    local redis_status wrk_status

    redis-cli -h 127.0.0.1 -p "$REDIS_PORT" flushall >/dev/null 2>&1 || true

    redis-benchmark -h 127.0.0.1 -p "$REDIS_PORT" \
        -c "$REDIS_BENCH_CLIENTS" -n "$REDIS_BENCH_REQUESTS" \
        -t get,set --csv > "$RESULT_DIR/$label.redis.csv" &
    local redis_bench_pid="$!"

    wrk -t"$WRK_THREADS" -c"$WRK_CONNECTIONS" -d"$WRK_DURATION" --latency \
        "http://127.0.0.1:$NGINX_PORT/" > "$RESULT_DIR/$label.wrk.txt" &
    local wrk_pid="$!"

    wait "$redis_bench_pid"
    redis_status="$?"
    wait "$wrk_pid"
    wrk_status="$?"
    [ "$redis_status" -eq 0 ] || fail "$label redis-benchmark failed"
    [ "$wrk_status" -eq 0 ] || fail "$label wrk failed"

    awk -f scripts/extract_redis_metrics.awk \
        "$RESULT_DIR/$label.redis.csv" > "$RESULT_DIR/$label.redis.summary.csv"
    python3 scripts/extract_wrk_metrics.py \
        "$RESULT_DIR/$label.wrk.txt" "$RESULT_DIR/$label.nginx.summary.csv"
}

record_control_values() {
    local label="$1"
    {
        printf '%s_latency_cpuset_cpus=%s\n' "$label" "$(cat "$LAT/cpuset.cpus" 2>/dev/null || true)"
        printf '%s_background_cpuset_cpus=%s\n' "$label" "$(cat "$BG/cpuset.cpus" 2>/dev/null || true)"
        printf '%s_latency_memory_low=%s\n' "$label" "$(cat "$LAT/memory.low" 2>/dev/null || true)"
        printf '%s_background_memory_high=%s\n' "$label" "$(cat "$BG/memory.high" 2>/dev/null || true)"
    } >> "$RESULT_DIR/measurements.env"
}

force_cpuset_probe_values() {
    # setup_cgroup_v2.sh already writes the desired cpuset values. Temporarily
    # widen them so this benchmark can prove the Agent writes them back.
    echo 0-7 > "$LAT/cpuset.cpus"
    echo 0-7 > "$BG/cpuset.cpus"
}

measure_phase() {
    local label="$1"
    local cpu_max="$2"
    local memory_enabled="$3"
    local usage_before periods_before throttled_before throttled_usec_before
    local usage_after periods_after throttled_after throttled_usec_after
    local start_ns end_ns

    usage_before="$(cpu_stat_value usage_usec)"
    periods_before="$(cpu_stat_value nr_periods)"
    throttled_before="$(cpu_stat_value nr_throttled)"
    throttled_usec_before="$(cpu_stat_value throttled_usec)"
    start_ns="$(date +%s%N)"

    run_mixed_benchmark "$label"

    end_ns="$(date +%s%N)"
    usage_after="$(cpu_stat_value usage_usec)"
    periods_after="$(cpu_stat_value nr_periods)"
    throttled_after="$(cpu_stat_value nr_throttled)"
    throttled_usec_after="$(cpu_stat_value throttled_usec)"

    local duration_ms=$(((end_ns - start_ns) / 1000000))
    {
        printf '%s_cpu_max=%s\n' "$label" "$cpu_max"
        printf '%s_memory_enabled=%s\n' "$label" "$memory_enabled"
        printf '%s_duration_ms=%s\n' "$label" "$duration_ms"
        printf '%s_usage_delta_usec=%s\n' "$label" "$((usage_after - usage_before))"
        printf '%s_nr_periods_delta=%s\n' "$label" "$((periods_after - periods_before))"
        printf '%s_nr_throttled_delta=%s\n' "$label" "$((throttled_after - throttled_before))"
        printf '%s_throttled_usec_delta=%s\n' "$label" "$((throttled_usec_after - throttled_usec_before))"
    } >> "$RESULT_DIR/measurements.env"
}

run_agent_phase() {
    local label="$1"
    local background_cpu_max="$2"
    local memory_enabled="$3"
    local latency_memory_low="$4"
    local background_memory_high="$5"
    local expected_cpu_max="$6"

    info "phase=$label cpu_max=$background_cpu_max memory_enabled=$memory_enabled"
    scripts/setup_cgroup_v2.sh > "$RESULT_DIR/setup.$label.log" 2>&1
    force_cpuset_probe_values
    write_agent_config "$label" "$background_cpu_max" "$memory_enabled" \
        "$latency_memory_low" "$background_memory_high"
    start_background_hogs "$HOG_WORKERS"

    timeout 60s ./build/eulerpilot-agent \
        --config "$RESULT_DIR/agent.$label.yaml" \
        --backend cgroup_v2 \
        --gate-mode always-active \
        --active \
        --duration-s "$AGENT_DURATION_S" \
        --interval-ms "$AGENT_INTERVAL_MS" \
        --jsonl \
        > "$RESULT_DIR/agent.$label.log" 2>&1 &
    AGENT_PID="$!"

    if [ -n "$expected_cpu_max" ]; then
        wait_for_value "$BG/cpu.max" "$expected_cpu_max" ||
            fail "$label cpu.max expected value was not applied"
    else
        sleep 2
    fi
    wait_for_value "$LAT/cpuset.cpus" "0-1" ||
        fail "$label latency cpuset was not available"
    wait_for_value "$BG/cpuset.cpus" "4-7" ||
        fail "$label background cpuset was not available"

    if [ "$memory_enabled" = "true" ]; then
        wait_for_value "$LAT/memory.low" "$latency_memory_low" ||
            fail "$label latency memory.low expected value was not applied"
        wait_for_value "$BG/memory.high" "$background_memory_high" ||
            fail "$label background memory.high expected value was not applied"
    fi

    record_control_values "$label"
    measure_phase "$label" "$background_cpu_max" "$memory_enabled"
    wait "$AGENT_PID"
    AGENT_PID=""
    cleanup_hogs
}

write_summary() {
    python3 - "$RESULT_DIR" "$BUSINESS_RETENTION_MIN" "${PHASE_LABELS[@]}" <<'PY'
import csv
import pathlib
import sys

result_dir = pathlib.Path(sys.argv[1])
business_retention_min = float(sys.argv[2])
labels = sys.argv[3:]

measurements = {}
for raw in (result_dir / "measurements.env").read_text().splitlines():
    if "=" in raw:
        key, value = raw.split("=", 1)
        measurements[key] = value

def metric(label, name, default="0"):
    return measurements.get(f"{label}_{name}", default)

def redis_rps(label, op):
    path = result_dir / f"{label}.redis.summary.csv"
    with path.open(newline="") as f:
        for row in csv.reader(f):
            if row and row[0] == op:
                return float(row[1])
    return 0.0

def nginx_value(label, name):
    path = result_dir / f"{label}.nginx.summary.csv"
    with path.open(newline="") as f:
        rows = list(csv.DictReader(f))
    if not rows:
        return ""
    return rows[0].get(name, "")

def nginx_rps(label):
    value = nginx_value(label, "requests_per_sec")
    return float(value) if value else 0.0

def bg_rate(label):
    usage = float(metric(label, "usage_delta_usec"))
    duration_ms = float(metric(label, "duration_ms"))
    return usage / (duration_ms / 1000.0) if duration_ms > 0 else 0.0

baseline = "cpu_cpuset_no_quota"
baseline_get = redis_rps(baseline, "GET")
baseline_set = redis_rps(baseline, "SET")
baseline_nginx = nginx_rps(baseline)
baseline_rate = bg_rate(baseline)

rows = []
for label in labels:
    get = redis_rps(label, "GET")
    set_ = redis_rps(label, "SET")
    nginx = nginx_rps(label)
    rate = bg_rate(label)
    get_ratio = get / baseline_get if baseline_get > 0 else 0.0
    set_ratio = set_ / baseline_set if baseline_set > 0 else 0.0
    nginx_ratio = nginx / baseline_nginx if baseline_nginx > 0 else 0.0
    bg_ratio = rate / baseline_rate if baseline_rate > 0 else 0.0
    business_min_ratio = min(get_ratio, set_ratio, nginx_ratio)
    rows.append({
        "label": label,
        "cpu_max": metric(label, "cpu_max", "unknown"),
        "memory_enabled": metric(label, "memory_enabled", "false"),
        "latency_cpuset_cpus": metric(label, "latency_cpuset_cpus", ""),
        "background_cpuset_cpus": metric(label, "background_cpuset_cpus", ""),
        "latency_memory_low": metric(label, "latency_memory_low", ""),
        "background_memory_high": metric(label, "background_memory_high", ""),
        "redis_get_rps": f"{get:.2f}",
        "redis_set_rps": f"{set_:.2f}",
        "nginx_requests_per_sec": f"{nginx:.2f}",
        "redis_get_ratio_vs_baseline": f"{get_ratio:.4f}",
        "redis_set_ratio_vs_baseline": f"{set_ratio:.4f}",
        "nginx_rps_ratio_vs_baseline": f"{nginx_ratio:.4f}",
        "business_min_ratio_vs_baseline": f"{business_min_ratio:.4f}",
        "nginx_p99_latency": nginx_value(label, "p99_latency"),
        "background_usage_rate_usec_per_s": f"{rate:.2f}",
        "background_ratio_vs_baseline": f"{bg_ratio:.4f}",
        "nr_throttled_delta": metric(label, "nr_throttled_delta"),
        "throttled_usec_delta": metric(label, "throttled_usec_delta"),
        "duration_ms": metric(label, "duration_ms"),
    })

with (result_dir / "profile_summary.csv").open("w", newline="") as f:
    writer = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
    writer.writeheader()
    writer.writerows(rows)

row_by_label = {row["label"]: row for row in rows}
multi50 = row_by_label["multi_quota50"]
multi20 = row_by_label["multi_quota20"]
cpu50 = row_by_label["cpu_cpuset_quota50"]

if baseline_get <= 0 or baseline_set <= 0 or baseline_nginx <= 0:
    raise SystemExit("baseline Redis/Nginx throughput is not positive")
if float(cpu50["background_ratio_vs_baseline"]) >= 0.30:
    raise SystemExit("cpu_cpuset_quota50 background ratio is too high")
if float(multi50["business_min_ratio_vs_baseline"]) < business_retention_min:
    raise SystemExit("multi_quota50 did not keep business retention threshold")
if float(multi50["background_ratio_vs_baseline"]) >= 0.20:
    raise SystemExit("multi_quota50 background ratio is too high")
if int(multi50["nr_throttled_delta"]) <= 0:
    raise SystemExit("multi_quota50 did not increase nr_throttled")
if int(multi50["throttled_usec_delta"]) <= 0:
    raise SystemExit("multi_quota50 did not increase throttled_usec")
if multi50["latency_cpuset_cpus"] != "0-1" or multi50["background_cpuset_cpus"] != "4-7":
    raise SystemExit("multi_quota50 cpuset values were not recorded")
if multi50["latency_memory_low"] != "67108864":
    raise SystemExit("multi_quota50 latency memory.low was not recorded")
if multi50["background_memory_high"] != "134217728":
    raise SystemExit("multi_quota50 background memory.high was not recorded")

recommended = multi50
if float(multi20["business_min_ratio_vs_baseline"]) >= business_retention_min:
    recommended = min(
        [multi50, multi20],
        key=lambda row: float(row["background_ratio_vs_baseline"]),
    )

summary = result_dir / "summary.txt"
with summary.open("w") as f:
    f.write("result=pass\n")
    f.write("benchmark=mixed_redis_nginx_multi_resource_profile\n")
    f.write(f"business_retention_min={business_retention_min:.2f}\n")
    f.write(f"baseline_redis_get_rps={baseline_get:.2f}\n")
    f.write(f"baseline_redis_set_rps={baseline_set:.2f}\n")
    f.write(f"baseline_nginx_requests_per_sec={baseline_nginx:.2f}\n")
    f.write(f"baseline_background_usage_rate_usec_per_s={baseline_rate:.2f}\n")
    f.write(f"cpu_cpuset_quota50_background_ratio_vs_baseline={cpu50['background_ratio_vs_baseline']}\n")
    f.write(f"multi_quota50_business_min_ratio_vs_baseline={multi50['business_min_ratio_vs_baseline']}\n")
    f.write(f"multi_quota50_background_ratio_vs_baseline={multi50['background_ratio_vs_baseline']}\n")
    f.write(f"multi_quota50_latency_memory_low={multi50['latency_memory_low']}\n")
    f.write(f"multi_quota50_background_memory_high={multi50['background_memory_high']}\n")
    f.write(f"multi_quota50_latency_cpuset_cpus={multi50['latency_cpuset_cpus']}\n")
    f.write(f"multi_quota50_background_cpuset_cpus={multi50['background_cpuset_cpus']}\n")
    f.write(f"recommended_profile={recommended['label']}\n")
    f.write(f"recommended_cpu_max={recommended['cpu_max']}\n")
    f.write(f"recommended_business_min_ratio_vs_baseline={recommended['business_min_ratio_vs_baseline']}\n")
    f.write(f"recommended_background_ratio_vs_baseline={recommended['background_ratio_vs_baseline']}\n")

report = result_dir / "report.md"
with report.open("w") as f:
    f.write("# Mixed Redis + Nginx Multi-Resource Profile Benchmark\n\n")
    f.write("- result: `pass`\n")
    f.write(f"- business retention threshold: `{business_retention_min:.2f}`\n")
    f.write(f"- recommended profile: `{recommended['label']}`\n")
    f.write(f"- recommended cpu.max: `{recommended['cpu_max']}`\n\n")
    f.write("## Profile Results\n\n")
    f.write("| Profile | cpu.max | Memory | Redis GET Ratio | Redis SET Ratio | Nginx RPS Ratio | Business Min Ratio | Nginx p99 | Background Ratio | latency memory.low | background memory.high |\n")
    f.write("|---------|---------|--------|-----------------|-----------------|-----------------|--------------------|-----------|------------------|--------------------|------------------------|\n")
    for row in rows:
        f.write(
            f"| {row['label']} | `{row['cpu_max']}` | {row['memory_enabled']} | "
            f"{row['redis_get_ratio_vs_baseline']} | {row['redis_set_ratio_vs_baseline']} | "
            f"{row['nginx_rps_ratio_vs_baseline']} | {row['business_min_ratio_vs_baseline']} | "
            f"{row['nginx_p99_latency']} | {row['background_ratio_vs_baseline']} | "
            f"`{row['latency_memory_low']}` | `{row['background_memory_high']}` |\n"
        )
    f.write("\n## Interpretation\n\n")
    f.write("This benchmark compares the existing CPU/cpuset placement path with a multi-resource profile that also writes latency `memory.low` and background `memory.high`. It keeps Redis and Nginx running together and uses Redis GET/SET plus Nginx RPS as foreground boundary signals. The goal is to prove that EulerPilot can apply a combined CPU, cpuset, and memory protection profile with audit and rollback evidence, not to claim universal business throughput improvement.\n\n")
    f.write("## Artifacts\n\n")
    f.write("- `profile_summary.csv`\n")
    f.write("- `summary.txt`\n")
    f.write("- `resource_control_events.jsonl`\n")
    f.write("- `agent.<profile>.log`\n")
    f.write("- `<profile>.redis.csv` and `<profile>.redis.summary.csv`\n")
    f.write("- `<profile>.wrk.txt` and `<profile>.nginx.summary.csv`\n")
PY
}

[ "$(id -u)" -eq 0 ] || fail 'resource_control mixed multi-resource benchmark must run as root'
mkdir -p "$RESULT_DIR"
: > "$RESULT_DIR/benchmark.log"
: > "$RESULT_DIR/measurements.env"

command -v redis-server >/dev/null 2>&1 || fail 'redis-server is required'
command -v redis-benchmark >/dev/null 2>&1 || fail 'redis-benchmark is required'
command -v nginx >/dev/null 2>&1 || fail 'nginx is required'
command -v wrk >/dev/null 2>&1 || fail 'wrk is required'
command -v curl >/dev/null 2>&1 || fail 'curl is required'

make agent
scripts/setup_cgroup_v2.sh > "$RESULT_DIR/setup.initial.log" 2>&1
mkdir -p reports/events run/eulerpilot
: > reports/events/resource_control.jsonl
: > run/eulerpilot/action_journal.jsonl

[ -w "$BG/cpu.max" ] || fail "$BG/cpu.max is not writable"
[ -f "$BG/cpu.stat" ] || fail "$BG/cpu.stat is missing"
[ -w "$LAT/memory.low" ] || fail "$LAT/memory.low is not writable"
[ -w "$BG/memory.high" ] || fail "$BG/memory.high is not writable"

start_redis
start_nginx
for i in "${!PHASE_LABELS[@]}"; do
    label="${PHASE_LABELS[$i]}"
    cpu_max="${PHASE_CPU_MAX[$i]}"
    memory_enabled="${PHASE_MEMORY_ENABLED[$i]}"
    latency_memory_low="${PHASE_LATENCY_MEMORY_LOW[$i]}"
    background_memory_high="${PHASE_BACKGROUND_MEMORY_HIGH[$i]}"
    expected="$cpu_max"
    if [ "$cpu_max" = "max" ]; then
        expected=""
    fi
    run_agent_phase "$label" "$cpu_max" "$memory_enabled" \
        "$latency_memory_low" "$background_memory_high" "$expected"
done

grep -q '"file":"cpuset.cpus"' reports/events/resource_control.jsonl ||
    fail 'resource_control audit log missing cpuset.cpus event'
grep -q '"file":"memory.low"' reports/events/resource_control.jsonl ||
    fail 'resource_control audit log missing memory.low event'
grep -q '"file":"memory.high"' reports/events/resource_control.jsonl ||
    fail 'resource_control audit log missing memory.high event'
grep -q '"result":"applied"' reports/events/resource_control.jsonl ||
    fail 'resource_control audit log missing applied result'
grep -q '"result":"restored"' reports/events/resource_control.jsonl ||
    fail 'resource_control audit log missing restored result'

cp reports/events/resource_control.jsonl "$RESULT_DIR/resource_control_events.jsonl"
write_summary

info "resource_control mixed multi-resource benchmark result saved to $RESULT_DIR"
