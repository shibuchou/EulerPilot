#!/usr/bin/env bash
set -euo pipefail

ROOT="/sys/fs/cgroup/eulerpilot"
LATENCY_WEIGHT="${LATENCY_WEIGHT:-1000}"
BATCH_WEIGHT="${BATCH_WEIGHT:-100}"
BACKGROUND_WEIGHT="${BACKGROUND_WEIGHT:-20}"
IO_WEIGHT="${IO_WEIGHT:-100}"

info() { printf '[INFO] %s\n' "$*"; }
fail() { printf '[FAIL] %s\n' "$*" >&2; exit 1; }

detect_root_io_device() {
    findmnt -no MAJ:MIN -T / 2>/dev/null | awk 'NF { print $1; exit }'
}

detect_effective_value() {
    local name="$1"
    local fallback="$2"
    local value=""
    for path in "$ROOT/${name}.effective" "/sys/fs/cgroup/${name}.effective"; do
        if [ -r "$path" ]; then
            value="$(cat "$path" 2>/dev/null || true)"
            if [ -n "$value" ]; then
                printf '%s\n' "$value"
                return 0
            fi
        fi
    done
    printf '%s\n' "$fallback"
}

enable_controller() {
    local subtree="$1"
    local controller="$2"
    [ -w "$subtree" ] || return 0
    if ! grep -qw "$controller" "$subtree" 2>/dev/null; then
        echo "+$controller" > "$subtree" 2>/dev/null || true
    fi
}

write_cgroup_value_or_fallback() {
    local file="$1"
    local value="$2"
    local fallback="$3"
    [ -w "$file" ] || return 0
    if ! echo "$value" > "$file" 2>/dev/null; then
        echo "$fallback" > "$file"
    fi
}

if ! mount | grep -q 'type cgroup2'; then
    fail 'cgroup v2 is not mounted on this system'
fi

mkdir -p "$ROOT"/latency "$ROOT"/batch "$ROOT"/background

if [ ! -d "$ROOT" ]; then
    fail "failed to create $ROOT"
fi

# Enable controllers for children when supported. Memory is required by the
# Resource Control closed loop; failures are tolerated so doctor/test can give
# a clearer environment-specific error.
for controller in cpu cpuset memory io; do
    enable_controller /sys/fs/cgroup/cgroup.subtree_control "$controller"
    enable_controller "$ROOT/cgroup.subtree_control" "$controller"
done

IO_DEVICE="${IO_DEVICE:-$(detect_root_io_device)}"
CPUSET_CPUS="${CPUSET_CPUS:-$(detect_effective_value cpuset.cpus "$(nproc --all 2>/dev/null | awk '{ if ($1 > 1) print "0-" $1 - 1; else print "0" }')")}"
CPUSET_MEMS="${CPUSET_MEMS:-$(detect_effective_value cpuset.mems 0)}"
LATENCY_CPUSET="${LATENCY_CPUSET:-0-1}"
BATCH_CPUSET="${BATCH_CPUSET:-2-3}"
BACKGROUND_CPUSET="${BACKGROUND_CPUSET:-4-7}"

write_cgroup_value_or_fallback "$ROOT/cpuset.mems" "$CPUSET_MEMS" 0
write_cgroup_value_or_fallback "$ROOT/cpuset.cpus" "$CPUSET_CPUS" "$CPUSET_CPUS"
write_cgroup_value_or_fallback "$ROOT/latency/cpuset.mems" "$CPUSET_MEMS" 0
write_cgroup_value_or_fallback "$ROOT/batch/cpuset.mems" "$CPUSET_MEMS" 0
write_cgroup_value_or_fallback "$ROOT/background/cpuset.mems" "$CPUSET_MEMS" 0

write_cgroup_value_or_fallback "$ROOT/latency/cpuset.cpus" "$LATENCY_CPUSET" "$CPUSET_CPUS"
write_cgroup_value_or_fallback "$ROOT/batch/cpuset.cpus" "$BATCH_CPUSET" "$CPUSET_CPUS"
write_cgroup_value_or_fallback "$ROOT/background/cpuset.cpus" "$BACKGROUND_CPUSET" "$CPUSET_CPUS"

echo "$LATENCY_WEIGHT" > "$ROOT/latency/cpu.weight"
echo "$BATCH_WEIGHT" > "$ROOT/batch/cpu.weight"
echo "$BACKGROUND_WEIGHT" > "$ROOT/background/cpu.weight"

for group in latency batch background; do
    group_path="$ROOT/$group"
    [ -w "$group_path/cpu.max" ] && echo max > "$group_path/cpu.max"
    [ -w "$group_path/memory.high" ] && echo max > "$group_path/memory.high"
    [ -w "$group_path/memory.low" ] && echo 0 > "$group_path/memory.low"
    [ -w "$group_path/memory.max" ] && echo max > "$group_path/memory.max"
    [ -w "$group_path/io.weight" ] && echo "default $IO_WEIGHT" > "$group_path/io.weight"
    if [ -n "$IO_DEVICE" ] && [ -w "$group_path/io.max" ]; then
        echo "$IO_DEVICE rbps=max wbps=max riops=max wiops=max" > "$group_path/io.max" || true
    fi
done

info "EulerPilot cgroup v2 hierarchy is ready under $ROOT"
info "  latency/cpu.weight=$LATENCY_WEIGHT"
info "  batch/cpu.weight=$BATCH_WEIGHT"
info "  background/cpu.weight=$BACKGROUND_WEIGHT"
info '  memory controller requested for latency/batch/background'
info "  io controller requested for latency/batch/background, io.device=${IO_DEVICE:-unknown}"
info "  latency/cpuset.cpus=$(cat "$ROOT/latency/cpuset.cpus" 2>/dev/null || true)"
info "  batch/cpuset.cpus=$(cat "$ROOT/batch/cpuset.cpus" 2>/dev/null || true)"
info "  background/cpuset.cpus=$(cat "$ROOT/background/cpuset.cpus" 2>/dev/null || true)"
