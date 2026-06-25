#!/usr/bin/env bash
set -euo pipefail

ROOT="/sys/fs/cgroup/eulerpilot"
BG="$ROOT/background"
RESULT_DIR="${RESULT_DIR:-results/resource_control/cpu-quota-$(date +%Y%m%d-%H%M%S)}"
BASELINE_SECONDS="${BASELINE_SECONDS:-4}"
LIMITED_SECONDS="${LIMITED_SECONDS:-6}"
AGENT_PID=""
HOG_PID=""
LIMITED_HOG_PID=""

fail() {
    printf '[FAIL] %s\n' "$*" >&2
    exit 1
}

info() {
    printf '[INFO] %s\n' "$*"
}

cleanup() {
    set +e
    if [ -n "$AGENT_PID" ] && kill -0 "$AGENT_PID" 2>/dev/null; then
        kill "$AGENT_PID" 2>/dev/null
        wait "$AGENT_PID" 2>/dev/null
    fi
    if [ -n "$HOG_PID" ] && kill -0 "$HOG_PID" 2>/dev/null; then
        kill "$HOG_PID" 2>/dev/null
        wait "$HOG_PID" 2>/dev/null
    fi
    scripts/rollback.sh > "$RESULT_DIR/rollback.log" 2>&1
}
trap cleanup EXIT

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

cpu_stat_value() {
    local key="$1"
    awk -v key="$key" '$1 == key { print $2; found=1 } END { if (!found) print 0 }' "$BG/cpu.stat"
}

start_hog_in_background_cgroup() {
    yes > /dev/null &
    HOG_PID="$!"
    echo "$HOG_PID" > "$BG/cgroup.procs"
    info "cpu hog pid=$HOG_PID"
}

stop_hog() {
    if [ -n "$HOG_PID" ] && kill -0 "$HOG_PID" 2>/dev/null; then
        kill "$HOG_PID" 2>/dev/null || true
        wait "$HOG_PID" 2>/dev/null || true
    fi
    HOG_PID=""
}

measure_cpu_window() {
    local label="$1"
    local seconds="$2"

    local usage_before periods_before throttled_before throttled_usec_before
    usage_before="$(cpu_stat_value usage_usec)"
    periods_before="$(cpu_stat_value nr_periods)"
    throttled_before="$(cpu_stat_value nr_throttled)"
    throttled_usec_before="$(cpu_stat_value throttled_usec)"

    sleep "$seconds"

    local usage_after periods_after throttled_after throttled_usec_after
    usage_after="$(cpu_stat_value usage_usec)"
    periods_after="$(cpu_stat_value nr_periods)"
    throttled_after="$(cpu_stat_value nr_throttled)"
    throttled_usec_after="$(cpu_stat_value throttled_usec)"

    local usage_delta=$((usage_after - usage_before))
    local periods_delta=$((periods_after - periods_before))
    local throttled_delta=$((throttled_after - throttled_before))
    local throttled_usec_delta=$((throttled_usec_after - throttled_usec_before))

    {
        printf '%s_seconds=%s\n' "$label" "$seconds"
        printf '%s_usage_usec_before=%s\n' "$label" "$usage_before"
        printf '%s_usage_usec_after=%s\n' "$label" "$usage_after"
        printf '%s_usage_delta_usec=%s\n' "$label" "$usage_delta"
        printf '%s_nr_periods_delta=%s\n' "$label" "$periods_delta"
        printf '%s_nr_throttled_delta=%s\n' "$label" "$throttled_delta"
        printf '%s_throttled_usec_delta=%s\n' "$label" "$throttled_usec_delta"
    } >> "$RESULT_DIR/measurements.env"
}

[ "$(id -u)" -eq 0 ] || fail 'resource_control cpu quota integration test must run as root'

mkdir -p "$RESULT_DIR"
trap cleanup EXIT

make agent
scripts/setup_cgroup_v2.sh > "$RESULT_DIR/setup.log" 2>&1
mkdir -p reports/events run/eulerpilot
: > reports/events/resource_control.jsonl
: > run/eulerpilot/action_journal.jsonl
: > "$RESULT_DIR/measurements.env"

[ -w "$BG/cpu.max" ] || fail "$BG/cpu.max is not writable"
[ -f "$BG/cpu.stat" ] || fail "$BG/cpu.stat is missing"

OLD_CPU_MAX="$(cat "$BG/cpu.max")"
echo max > "$BG/cpu.max"

start_hog_in_background_cgroup
measure_cpu_window baseline "$BASELINE_SECONDS"
stop_hog

cat > "$RESULT_DIR/agent.resource-control-cpu-quota.yaml" <<'YAML'
agent:
  name: EulerPilot
  mode: active
skills_config_path: skills.resource-control-cpu-quota.yaml
scheduler:
  type: cgroup_v2
exporter:
  prometheus:
    enabled: false
YAML

cat > "$RESULT_DIR/skills.resource-control-cpu-quota.yaml" <<'YAML'
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

yes > /dev/null &
HOG_PID="$!"
LIMITED_HOG_PID="$HOG_PID"
info "limited cpu hog pid=$HOG_PID"

timeout 35s ./build/eulerpilot-agent \
    --config "$RESULT_DIR/agent.resource-control-cpu-quota.yaml" \
    --backend cgroup_v2 \
    --gate-mode always-active \
    --active \
    --duration-s 18 \
    --interval-ms 500 \
    --jsonl \
    > "$RESULT_DIR/agent.log" 2>&1 &
AGENT_PID="$!"

wait_for_value "$BG/cpu.max" '10000 100000' || fail 'cpu.max pressure value was not applied'
if ! grep -qw "$HOG_PID" "$BG/cgroup.procs"; then
    fail 'limited workload was not moved into the EulerPilot background cgroup'
fi
measure_cpu_window limited "$LIMITED_SECONDS"

wait "$AGENT_PID"
AGENT_PID=""
stop_hog

wait_for_value "$BG/cpu.max" "$OLD_CPU_MAX" || fail 'cpu.max was not restored after agent stop'

grep -q '"file":"cpu.max"' reports/events/resource_control.jsonl ||
    fail 'resource_control audit log missing cpu.max event'
grep -q '"result":"applied"' reports/events/resource_control.jsonl ||
    fail 'resource_control audit log missing applied result'
grep -q '"result":"restored"' reports/events/resource_control.jsonl ||
    fail 'resource_control audit log missing restored result'

# shellcheck disable=SC1090
. "$RESULT_DIR/measurements.env"

read -r BASELINE_RATE LIMITED_RATE RATIO < <(
    python3 - "$baseline_usage_delta_usec" "$baseline_seconds" \
           "$limited_usage_delta_usec" "$limited_seconds" <<'PY'
import sys
baseline_usage = float(sys.argv[1])
baseline_seconds = float(sys.argv[2])
limited_usage = float(sys.argv[3])
limited_seconds = float(sys.argv[4])
baseline_rate = baseline_usage / baseline_seconds
limited_rate = limited_usage / limited_seconds
ratio = limited_rate / baseline_rate if baseline_rate > 0 else 0.0
print(f"{baseline_rate:.2f} {limited_rate:.2f} {ratio:.4f}")
PY
)

python3 - "$RATIO" "$limited_nr_throttled_delta" "$limited_throttled_usec_delta" <<'PY'
import sys
ratio = float(sys.argv[1])
nr_throttled = int(sys.argv[2])
throttled_usec = int(sys.argv[3])
if ratio >= 0.70:
    raise SystemExit(f"limited CPU usage rate ratio too high: {ratio:.4f}")
if nr_throttled <= 0:
    raise SystemExit("nr_throttled did not increase under cpu.max")
if throttled_usec <= 0:
    raise SystemExit("throttled_usec did not increase under cpu.max")
PY

cp reports/events/resource_control.jsonl "$RESULT_DIR/resource_control_events.jsonl"
cat > "$RESULT_DIR/summary.txt" <<EOF_SUMMARY
result=pass
hog_pid=$LIMITED_HOG_PID
cpu_max_pressure=10000 100000
old_cpu_max=$OLD_CPU_MAX
baseline_seconds=$baseline_seconds
limited_seconds=$limited_seconds
baseline_usage_delta_usec=$baseline_usage_delta_usec
limited_usage_delta_usec=$limited_usage_delta_usec
baseline_usage_rate_usec_per_s=$BASELINE_RATE
limited_usage_rate_usec_per_s=$LIMITED_RATE
usage_rate_ratio=$RATIO
baseline_nr_periods_delta=$baseline_nr_periods_delta
limited_nr_periods_delta=$limited_nr_periods_delta
baseline_nr_throttled_delta=$baseline_nr_throttled_delta
limited_nr_throttled_delta=$limited_nr_throttled_delta
baseline_throttled_usec_delta=$baseline_throttled_usec_delta
limited_throttled_usec_delta=$limited_throttled_usec_delta
EOF_SUMMARY

info "resource_control cpu quota integration result saved to $RESULT_DIR"
