#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

CGROUP_ROOT="/sys/fs/cgroup/eulerpilot"
BG="$CGROUP_ROOT/background"
NGINX_PORT="${NGINX_PORT:-18083}"
WRK_THREADS="${WRK_THREADS:-2}"
WRK_CONNECTIONS="${WRK_CONNECTIONS:-32}"
WRK_DURATION="${WRK_DURATION:-10s}"
HOG_WORKERS="${HOG_WORKERS:-4}"
AGENT_DURATION_S="${AGENT_DURATION_S:-28}"
AGENT_INTERVAL_MS="${AGENT_INTERVAL_MS:-500}"
RPS_RETENTION_MIN="${RPS_RETENTION_MIN:-0.85}"
RESULT_DIR="${RESULT_DIR:-results/resource_control/nginx-quota-sweep-$(date +%Y%m%d-%H%M%S)}"
NGINX_DIR="$ROOT/$RESULT_DIR/nginx"
NGINX_CONF="$NGINX_DIR/nginx.conf"
NGINX_PID=""
AGENT_PID=""
HOG_PIDS=()

PROFILE_LABELS=(no_quota quota_50 quota_20 quota_10 quota_05)
PROFILE_CPU_MAX=("max" "50000 100000" "20000 100000" "10000 100000" "5000 100000")

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

cleanup_nginx() {
    if [ -f "$NGINX_DIR/nginx.pid" ]; then
        nginx -p "$NGINX_DIR" -c "$NGINX_CONF" -s quit >/dev/null 2>&1 || true
        kill "$(cat "$NGINX_DIR/nginx.pid" 2>/dev/null || true)" 2>/dev/null || true
    fi
    if [ -n "$NGINX_PID" ] && kill -0 "$NGINX_PID" 2>/dev/null; then
        kill "$NGINX_PID" 2>/dev/null || true
        wait "$NGINX_PID" 2>/dev/null || true
    fi
}

cleanup() {
    set +e
    cleanup_hogs
    cleanup_agent
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
            return 200 'EulerPilot nginx resource-control quota sweep\n';
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
          cpu_max: '$background_cpu_max'
- name: psi_gate
  kind: runtime
  enabled: true
  config: {}
YAML
}

run_wrk_benchmark() {
    local label="$1"
    wrk -t"$WRK_THREADS" -c"$WRK_CONNECTIONS" -d"$WRK_DURATION" --latency \
        "http://127.0.0.1:$NGINX_PORT/" > "$RESULT_DIR/$label.wrk.txt"
    python3 scripts/extract_wrk_metrics.py \
        "$RESULT_DIR/$label.wrk.txt" "$RESULT_DIR/$label.summary.csv"
}

measure_phase() {
    local label="$1"
    local cpu_max="$2"
    local usage_before periods_before throttled_before throttled_usec_before
    local usage_after periods_after throttled_after throttled_usec_after
    local start_ns end_ns

    usage_before="$(cpu_stat_value usage_usec)"
    periods_before="$(cpu_stat_value nr_periods)"
    throttled_before="$(cpu_stat_value nr_throttled)"
    throttled_usec_before="$(cpu_stat_value throttled_usec)"
    start_ns="$(date +%s%N)"

    run_wrk_benchmark "$label"

    end_ns="$(date +%s%N)"
    usage_after="$(cpu_stat_value usage_usec)"
    periods_after="$(cpu_stat_value nr_periods)"
    throttled_after="$(cpu_stat_value nr_throttled)"
    throttled_usec_after="$(cpu_stat_value throttled_usec)"

    local duration_ms=$(((end_ns - start_ns) / 1000000))
    {
        printf '%s_cpu_max=%s\n' "$label" "$cpu_max"
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
    local expected_cpu_max="$3"

    info "phase=$label cpu_max=$background_cpu_max"
    scripts/setup_cgroup_v2.sh > "$RESULT_DIR/setup.$label.log" 2>&1
    write_agent_config "$label" "$background_cpu_max"
    start_background_hogs "$HOG_WORKERS"

    timeout 50s ./build/eulerpilot-agent \
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

    measure_phase "$label" "$background_cpu_max"
    wait "$AGENT_PID"
    AGENT_PID=""
    cleanup_hogs
}

write_summary() {
    python3 - "$RESULT_DIR" "$RPS_RETENTION_MIN" "${PROFILE_LABELS[@]}" <<'PY'
import csv
import pathlib
import sys

result_dir = pathlib.Path(sys.argv[1])
rps_retention_min = float(sys.argv[2])
labels = sys.argv[3:]

measurements = {}
for raw in (result_dir / "measurements.env").read_text().splitlines():
    if "=" in raw:
        key, value = raw.split("=", 1)
        measurements[key] = value

def metric(label, name, default="0"):
    return measurements.get(f"{label}_{name}", default)

def summary_value(label, name):
    path = result_dir / f"{label}.summary.csv"
    with path.open(newline="") as f:
        rows = list(csv.DictReader(f))
    if not rows:
        return ""
    return rows[0].get(name, "")

def rps(label):
    value = summary_value(label, "requests_per_sec")
    return float(value) if value else 0.0

def bg_rate(label):
    usage = float(metric(label, "usage_delta_usec"))
    duration_ms = float(metric(label, "duration_ms"))
    return usage / (duration_ms / 1000.0) if duration_ms > 0 else 0.0

rows = []
no_quota_rps = rps("no_quota")
no_quota_rate = bg_rate("no_quota")

for label in labels:
    current_rps = rps(label)
    rate = bg_rate(label)
    rps_ratio = current_rps / no_quota_rps if no_quota_rps > 0 else 0.0
    bg_ratio = rate / no_quota_rate if no_quota_rate > 0 else 0.0
    rows.append({
        "label": label,
        "cpu_max": metric(label, "cpu_max", "unknown"),
        "requests_per_sec": f"{current_rps:.2f}",
        "rps_ratio_vs_no_quota": f"{rps_ratio:.4f}",
        "p99_latency": summary_value(label, "p99_latency"),
        "avg_latency": summary_value(label, "avg_latency"),
        "background_usage_rate_usec_per_s": f"{rate:.2f}",
        "background_ratio_vs_no_quota": f"{bg_ratio:.4f}",
        "nr_throttled_delta": metric(label, "nr_throttled_delta"),
        "throttled_usec_delta": metric(label, "throttled_usec_delta"),
        "duration_ms": metric(label, "duration_ms"),
    })

csv_path = result_dir / "sweep_summary.csv"
with csv_path.open("w", newline="") as f:
    writer = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
    writer.writeheader()
    writer.writerows(rows)

eligible = [
    row for row in rows
    if row["label"] != "no_quota"
    and float(row["rps_ratio_vs_no_quota"]) >= rps_retention_min
]
if eligible:
    recommended = min(eligible, key=lambda row: float(row["background_ratio_vs_no_quota"]))
    recommendation_reason = f"rps_retention_ge_{rps_retention_min:.2f}_and_min_background_ratio"
else:
    quota_rows = [row for row in rows if row["label"] != "no_quota"]
    recommended = max(quota_rows, key=lambda row: float(row["rps_ratio_vs_no_quota"]))
    recommendation_reason = "no_profile_met_rps_retention_threshold"

quota_10 = next(row for row in rows if row["label"] == "quota_10")
if no_quota_rps <= 0:
    raise SystemExit("no_quota Nginx requests/sec is not positive")
if float(quota_10["background_ratio_vs_no_quota"]) >= 0.30:
    raise SystemExit("quota_10 background ratio is too high")
if int(quota_10["nr_throttled_delta"]) <= 0:
    raise SystemExit("quota_10 did not increase nr_throttled")
if int(quota_10["throttled_usec_delta"]) <= 0:
    raise SystemExit("quota_10 did not increase throttled_usec")

summary = result_dir / "summary.txt"
with summary.open("w") as f:
    f.write("result=pass\n")
    f.write("benchmark=nginx_background_cpu_quota_sweep\n")
    f.write(f"rps_retention_min={rps_retention_min:.2f}\n")
    f.write(f"no_quota_requests_per_sec={no_quota_rps:.2f}\n")
    f.write(f"no_quota_background_usage_rate_usec_per_s={no_quota_rate:.2f}\n")
    f.write(f"quota_10_background_ratio_vs_no_quota={quota_10['background_ratio_vs_no_quota']}\n")
    f.write(f"quota_10_nr_throttled_delta={quota_10['nr_throttled_delta']}\n")
    f.write(f"quota_10_throttled_usec_delta={quota_10['throttled_usec_delta']}\n")
    f.write(f"recommended_profile={recommended['label']}\n")
    f.write(f"recommended_cpu_max={recommended['cpu_max']}\n")
    f.write(f"recommended_rps_ratio_vs_no_quota={recommended['rps_ratio_vs_no_quota']}\n")
    f.write(f"recommended_background_ratio_vs_no_quota={recommended['background_ratio_vs_no_quota']}\n")
    f.write(f"recommended_p99_latency={recommended['p99_latency']}\n")
    f.write(f"recommendation_reason={recommendation_reason}\n")

report = result_dir / "report.md"
with report.open("w") as f:
    f.write("# Nginx Background CPU Quota Sweep Benchmark\n\n")
    f.write("- result: `pass`\n")
    f.write(f"- RPS retention threshold: `{rps_retention_min:.2f}`\n")
    f.write(f"- recommended profile: `{recommended['label']}`\n")
    f.write(f"- recommended cpu.max: `{recommended['cpu_max']}`\n")
    f.write(f"- recommendation reason: `{recommendation_reason}`\n\n")
    f.write("## Sweep Results\n\n")
    f.write("| Profile | cpu.max | RPS | RPS Ratio | p99 Latency | Background Ratio | nr_throttled | throttled_usec |\n")
    f.write("|---------|---------|-----|-----------|-------------|------------------|--------------|----------------|\n")
    for row in rows:
        f.write(
            f"| {row['label']} | `{row['cpu_max']}` | {row['requests_per_sec']} | "
            f"{row['rps_ratio_vs_no_quota']} | {row['p99_latency']} | "
            f"{row['background_ratio_vs_no_quota']} | {row['nr_throttled_delta']} | "
            f"{row['throttled_usec_delta']} |\n"
        )
    f.write("\n## Interpretation\n\n")
    f.write("This benchmark keeps the Agent placement path consistent and sweeps only the background `cpu.max` pressure profile. The goal is to find a practical resource-control profile that suppresses background CPU pressure while preserving Nginx requests/sec as a boundary signal. It does not claim Nginx performance improvement unless the measured RPS ratios support that conclusion.\n\n")
    f.write("## Artifacts\n\n")
    f.write("- `sweep_summary.csv`\n")
    f.write("- `summary.txt`\n")
    f.write("- `resource_control_events.jsonl`\n")
    f.write("- `agent.<profile>.log`\n")
    f.write("- `<profile>.wrk.txt` and `<profile>.summary.csv`\n")
PY
}

[ "$(id -u)" -eq 0 ] || fail 'resource_control nginx quota sweep benchmark must run as root'
mkdir -p "$RESULT_DIR"
: > "$RESULT_DIR/benchmark.log"
: > "$RESULT_DIR/measurements.env"

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

start_nginx
for i in "${!PROFILE_LABELS[@]}"; do
    label="${PROFILE_LABELS[$i]}"
    cpu_max="${PROFILE_CPU_MAX[$i]}"
    expected="$cpu_max"
    if [ "$cpu_max" = "max" ]; then
        expected=""
    fi
    run_agent_phase "$label" "$cpu_max" "$expected"
done

grep -q '"file":"cpu.max"' reports/events/resource_control.jsonl ||
    fail 'resource_control audit log missing cpu.max event'
grep -q '"result":"applied"' reports/events/resource_control.jsonl ||
    fail 'resource_control audit log missing applied result'
grep -q '"result":"restored"' reports/events/resource_control.jsonl ||
    fail 'resource_control audit log missing restored result'

cp reports/events/resource_control.jsonl "$RESULT_DIR/resource_control_events.jsonl"
write_summary

info "resource_control Nginx quota sweep benchmark result saved to $RESULT_DIR"