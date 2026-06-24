#!/usr/bin/env bash
set -euo pipefail

ROOT="/sys/fs/cgroup/eulerpilot"
BG="$ROOT/background"
RESULT_DIR="${RESULT_DIR:-results/resource_control/io-$(date +%Y%m%d-%H%M%S)}"
AGENT_PID=""
HOG_PID=""

fail() {
    printf '[FAIL] %s\n' "$*" >&2
    exit 1
}

info() {
    printf '[INFO] %s\n' "$*"
}

detect_root_io_device() {
    findmnt -no MAJ:MIN -T / 2>/dev/null | awk 'NF { print $1; exit }'
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

wait_for_io_limit() {
    local deadline=$((SECONDS + 15))
    while [ "$SECONDS" -lt "$deadline" ]; do
        if grep -q "^$IO_DEVICE " "$BG/io.max" 2>/dev/null &&
           grep -q 'wbps=1048576' "$BG/io.max" 2>/dev/null &&
           grep -q '^default 50' "$BG/io.weight" 2>/dev/null; then
            return 0
        fi
        sleep 0.2
    done
    printf '[DEBUG] io.max=%s\n' "$(cat "$BG/io.max" 2>/dev/null || true)" >&2
    printf '[DEBUG] io.weight=%s\n' "$(cat "$BG/io.weight" 2>/dev/null || true)" >&2
    return 1
}

wait_for_restored_io() {
    local deadline=$((SECONDS + 15))
    while [ "$SECONDS" -lt "$deadline" ]; do
        local current_max current_weight
        current_max="$(cat "$BG/io.max" 2>/dev/null || true)"
        current_weight="$(cat "$BG/io.weight" 2>/dev/null || true)"
        if [ "$current_max" = "$OLD_IO_MAX" ] && [ "$current_weight" = "$OLD_IO_WEIGHT" ]; then
            return 0
        fi
        sleep 0.2
    done
    printf '[DEBUG] restored io.max=%s expected=%s\n' "$(cat "$BG/io.max" 2>/dev/null || true)" "$OLD_IO_MAX" >&2
    printf '[DEBUG] restored io.weight=%s expected=%s\n' "$(cat "$BG/io.weight" 2>/dev/null || true)" "$OLD_IO_WEIGHT" >&2
    return 1
}

read_wbytes() {
    awk -v dev="$IO_DEVICE" '
        $1 == dev {
            for (i = 2; i <= NF; i++) {
                if ($i ~ /^wbytes=/) {
                    split($i, a, "=");
                    print a[2];
                    found = 1;
                    exit;
                }
            }
        }
        END { if (!found) print 0; }
    ' "$BG/io.stat" 2>/dev/null
}

run_direct_write() {
    local label="$1"
    local count_mb="$2"
    local out_file="$RESULT_DIR/${label}.bin"
    local start_ns end_ns

    rm -f "$out_file"
    start_ns="$(date +%s%N)"
    bash -c 'echo $$ > "$1/cgroup.procs"; dd if=/dev/zero of="$2" bs=1M count="$3" oflag=direct status=none' \
        _ "$BG" "$out_file" "$count_mb"
    end_ns="$(date +%s%N)"
    awk -v start="$start_ns" -v end="$end_ns" 'BEGIN { printf "%.3f\n", (end - start) / 1000000000 }'
}

[ "$(id -u)" -eq 0 ] || fail 'resource_control IO integration test must run as root'

mkdir -p "$RESULT_DIR"
trap cleanup EXIT

IO_DEVICE="${IO_DEVICE:-$(detect_root_io_device)}"
[ -n "$IO_DEVICE" ] || fail 'failed to detect root filesystem block device'

make agent
IO_DEVICE="$IO_DEVICE" scripts/setup_cgroup_v2.sh > "$RESULT_DIR/setup.log" 2>&1
mkdir -p reports/events run/eulerpilot
: > reports/events/resource_control.jsonl
: > run/eulerpilot/action_journal.jsonl

[ -w "$BG/io.max" ] || fail "$BG/io.max is not writable"
[ -w "$BG/io.weight" ] || fail "$BG/io.weight is not writable"
[ -f "$BG/io.stat" ] || fail "$BG/io.stat is missing"

echo "$IO_DEVICE rbps=max wbps=max riops=max wiops=max" > "$BG/io.max"
echo 'default 100' > "$BG/io.weight"
OLD_IO_MAX="$(cat "$BG/io.max")"
OLD_IO_WEIGHT="$(cat "$BG/io.weight")"

BASELINE_BEFORE="$(read_wbytes)"
BASELINE_TIME="$(run_direct_write baseline 4)"
BASELINE_AFTER="$(read_wbytes)"
[ "$BASELINE_AFTER" -gt "$BASELINE_BEFORE" ] || fail 'baseline direct write did not increase io.stat wbytes'

yes > /dev/null &
HOG_PID="$!"
info "background hog pid=$HOG_PID io_device=$IO_DEVICE baseline_time=${BASELINE_TIME}s"

cat > "$RESULT_DIR/agent.resource-control-io.yaml" <<'YAML'
agent:
  name: EulerPilot
  mode: active
skills_config_path: skills.resource-control-io.yaml
scheduler:
  type: cgroup_v2
exporter:
  prometheus:
    enabled: false
YAML

cat > "$RESULT_DIR/skills.resource-control-io.yaml" <<YAML
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
        enabled: true
        high:
          enabled: true
        low:
          enabled: true
        max:
          enabled: true
        reclaim:
          enabled: false
      io:
        enabled: true
        device: '$IO_DEVICE'
        weight:
          enabled: true
        max:
          enabled: true
    profiles:
      latency:
        cpu_max: max
        memory_low: '67108864'
        memory_high: max
        memory_max: max
        io_weight: 'default 100'
        io_max: ''
      background:
        normal:
          cpu_max: max
          memory_high: max
          io_weight: 'default 100'
          io_max: ''
        pressure:
          cpu_max: '10000 100000'
          memory_high: '1048576'
          memory_low: '0'
          memory_max: max
          io_weight: 'default 50'
          io_max: '$IO_DEVICE rbps=max wbps=1048576'
- name: psi_gate
  kind: runtime
  enabled: true
  config: {}
YAML

timeout 35s ./build/eulerpilot-agent \
    --config "$RESULT_DIR/agent.resource-control-io.yaml" \
    --backend cgroup_v2 \
    --gate-mode always-active \
    --active \
    --duration-s 25 \
    --interval-ms 500 \
    --jsonl \
    > "$RESULT_DIR/agent.log" 2>&1 &
AGENT_PID="$!"

wait_for_io_limit || fail 'io.max/io.weight pressure values were not applied'

LIMITED_BEFORE="$(read_wbytes)"
LIMITED_TIME="$(run_direct_write limited 4)"
LIMITED_AFTER="$(read_wbytes)"
[ "$LIMITED_AFTER" -gt "$LIMITED_BEFORE" ] || fail 'limited direct write did not increase io.stat wbytes'
rm -f "$RESULT_DIR/baseline.bin" "$RESULT_DIR/limited.bin"

awk -v t="$LIMITED_TIME" 'BEGIN { exit !(t >= 2.5) }' ||
    fail "io.max did not visibly throttle the limited write: limited_time=${LIMITED_TIME}s"
awk -v b="$BASELINE_TIME" -v l="$LIMITED_TIME" 'BEGIN { exit !(l > b) }' ||
    fail "limited write was not slower than baseline: baseline=${BASELINE_TIME}s limited=${LIMITED_TIME}s"

wait "$AGENT_PID"
AGENT_PID=""

wait_for_restored_io || fail 'io.max/io.weight were not restored after agent stop'

grep -q '"file":"io.max"' reports/events/resource_control.jsonl || fail 'resource_control audit log missing io.max event'
grep -q '"file":"io.weight"' reports/events/resource_control.jsonl || fail 'resource_control audit log missing io.weight event'
grep -q '"result":"applied"' reports/events/resource_control.jsonl || fail 'resource_control audit log missing applied result'
grep -q '"result":"restored"' reports/events/resource_control.jsonl || fail 'resource_control audit log missing restored result'

cp reports/events/resource_control.jsonl "$RESULT_DIR/resource_control_events.jsonl"
cat > "$RESULT_DIR/summary.txt" <<EOF_SUMMARY
result=pass
io_device=$IO_DEVICE
hog_pid=$HOG_PID
io_max_pressure=$IO_DEVICE rbps=max wbps=1048576
io_weight_pressure=default 50
baseline_time_s=$BASELINE_TIME
limited_time_s=$LIMITED_TIME
baseline_wbytes_before=$BASELINE_BEFORE
baseline_wbytes_after=$BASELINE_AFTER
limited_wbytes_before=$LIMITED_BEFORE
limited_wbytes_after=$LIMITED_AFTER
old_io_max=$OLD_IO_MAX
old_io_weight=$OLD_IO_WEIGHT
EOF_SUMMARY

info "resource_control IO integration result saved to $RESULT_DIR"
