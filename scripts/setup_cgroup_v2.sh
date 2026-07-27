#!/usr/bin/env bash
set -euo pipefail

ROOT="/sys/fs/cgroup/eulerpilot"
LATENCY_WEIGHT="${LATENCY_WEIGHT:-1000}"
BATCH_WEIGHT="${BATCH_WEIGHT:-100}"
BACKGROUND_WEIGHT="${BACKGROUND_WEIGHT:-20}"
IO_WEIGHT="${IO_WEIGHT:-100}"
ENABLE_CPUSET="${EULERPILOT_ENABLE_CPUSET:-0}"

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

compute_dynamic_cpusets() {
    local effective="$1"
    local online
    online="$(cat /sys/devices/system/cpu/online 2>/dev/null || printf '%s' "$effective")"
    python3 - "$effective" "$online" "${EULERPILOT_ALLOW_2CPU_CPUSET:-0}" <<'PY'
import sys

def expand(spec):
    out = set()
    for part in (spec or "").split(","):
        part = part.strip()
        if not part:
            continue
        if "-" in part:
            start, end = part.split("-", 1)
            out.update(range(int(start), int(end) + 1))
        else:
            out.add(int(part))
    return out

def compress(values):
    values = sorted(values)
    if not values:
        return ""
    ranges = []
    start = prev = values[0]
    for value in values[1:]:
        if value == prev + 1:
            prev = value
            continue
        ranges.append(f"{start}-{prev}" if start != prev else str(start))
        start = prev = value
    ranges.append(f"{start}-{prev}" if start != prev else str(start))
    return ",".join(ranges)

effective = expand(sys.argv[1])
online = expand(sys.argv[2])
cpus = sorted(effective & online)
if not cpus:
    print("CPUSET_DEGRADED=1")
    print("DYNAMIC_LATENCY_CPUSET=")
    print("DYNAMIC_BATCH_CPUSET=")
    print("DYNAMIC_BACKGROUND_CPUSET=")
    raise SystemExit(0)

allow_two = sys.argv[3] == "1"
count = len(cpus)
if count == 1:
    latency = batch = background = cpus
    degraded = 1
elif count == 2 and not allow_two:
    latency = batch = background = cpus
    degraded = 1
elif count == 2:
    latency = [cpus[0]]
    batch = cpus
    background = [cpus[1]]
    degraded = 0
elif count == 3:
    latency = [cpus[0]]
    batch = [cpus[1]]
    background = [cpus[2]]
    degraded = 0
else:
    latency_n = max(1, count // 4)
    batch_n = max(1, count // 4)
    latency = cpus[:latency_n]
    batch = cpus[latency_n:latency_n + batch_n]
    background = cpus[latency_n + batch_n:]
    if not background:
        background = [batch.pop()]
    degraded = 0

print(f"CPUSET_DEGRADED={degraded}")
print(f"DYNAMIC_LATENCY_CPUSET={compress(latency)}")
print(f"DYNAMIC_BATCH_CPUSET={compress(batch)}")
print(f"DYNAMIC_BACKGROUND_CPUSET={compress(background)}")
PY
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
CONTROLLERS="cpu memory io"
if [ "$ENABLE_CPUSET" = "1" ]; then
    CONTROLLERS="$CONTROLLERS cpuset"
fi
for controller in $CONTROLLERS; do
    enable_controller /sys/fs/cgroup/cgroup.subtree_control "$controller"
    enable_controller "$ROOT/cgroup.subtree_control" "$controller"
done

IO_DEVICE="${IO_DEVICE:-$(detect_root_io_device)}"
if [ "$ENABLE_CPUSET" = "1" ]; then
    CPUSET_CPUS="${CPUSET_CPUS:-$(detect_effective_value cpuset.cpus "$(nproc --all 2>/dev/null | awk '{ if ($1 > 1) print "0-" $1 - 1; else print "0" }')")}"
    CPUSET_MEMS="${CPUSET_MEMS:-$(detect_effective_value cpuset.mems 0)}"
    eval "$(compute_dynamic_cpusets "$CPUSET_CPUS")"
    LATENCY_CPUSET="${LATENCY_CPUSET:-$DYNAMIC_LATENCY_CPUSET}"
    BATCH_CPUSET="${BATCH_CPUSET:-$DYNAMIC_BATCH_CPUSET}"
    BACKGROUND_CPUSET="${BACKGROUND_CPUSET:-$DYNAMIC_BACKGROUND_CPUSET}"

    write_cgroup_value_or_fallback "$ROOT/cpuset.mems" "$CPUSET_MEMS" 0
    write_cgroup_value_or_fallback "$ROOT/cpuset.cpus" "$CPUSET_CPUS" "$CPUSET_CPUS"
    write_cgroup_value_or_fallback "$ROOT/latency/cpuset.mems" "$CPUSET_MEMS" 0
    write_cgroup_value_or_fallback "$ROOT/batch/cpuset.mems" "$CPUSET_MEMS" 0
    write_cgroup_value_or_fallback "$ROOT/background/cpuset.mems" "$CPUSET_MEMS" 0

    write_cgroup_value_or_fallback "$ROOT/latency/cpuset.cpus" "$LATENCY_CPUSET" "$CPUSET_CPUS"
    write_cgroup_value_or_fallback "$ROOT/batch/cpuset.cpus" "$BATCH_CPUSET" "$CPUSET_CPUS"
    write_cgroup_value_or_fallback "$ROOT/background/cpuset.cpus" "$BACKGROUND_CPUSET" "$CPUSET_CPUS"
else
    CPUSET_DEGRADED=0
fi

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
if [ "$ENABLE_CPUSET" = "1" ]; then
    info "  cpuset_control=enabled"
    info "  latency/cpuset.cpus=$(cat "$ROOT/latency/cpuset.cpus" 2>/dev/null || true)"
    info "  batch/cpuset.cpus=$(cat "$ROOT/batch/cpuset.cpus" 2>/dev/null || true)"
    info "  background/cpuset.cpus=$(cat "$ROOT/background/cpuset.cpus" 2>/dev/null || true)"
    if [ "${CPUSET_DEGRADED:-0}" = "1" ]; then
        info '  cpuset split degraded: using full effective CPU set for one or more groups'
    fi
else
    info '  cpuset_control=disabled (release default; set EULERPILOT_ENABLE_CPUSET=1 for experimental cpuset split)'
fi
