#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT_DIR/reports/gates/capability_matrix.json}"
mkdir -p "$(dirname "$OUT")"

json_escape() {
    python3 -c 'import json,sys; print(json.dumps(sys.stdin.read().strip()))'
}

kernel_config_value() {
    local key="$1"
    if [[ -r /proc/config.gz ]]; then
        zgrep -E "^${key}=" /proc/config.gz | head -n 1 | cut -d= -f2- || true
    elif [[ -r "/boot/config-$(uname -r)" ]]; then
        grep -E "^${key}=" "/boot/config-$(uname -r)" | head -n 1 | cut -d= -f2- || true
    fi
}

bool_file_readable() {
    [[ -r "$1" ]] && printf true || printf false
}

bool_path_exists() {
    [[ -e "$1" ]] && printf true || printf false
}

tool_path() {
    command -v "$1" 2>/dev/null || true
}

lsm_list=""
if [[ -r /sys/kernel/security/lsm ]]; then
    lsm_list="$(cat /sys/kernel/security/lsm)"
fi

os_pretty=""
if [[ -r /etc/os-release ]]; then
    os_pretty="$(. /etc/os-release && printf '%s' "${PRETTY_NAME:-}")"
fi

bpf_lsm_available=false
if [[ "$lsm_list" == *bpf* ]]; then
    bpf_lsm_available=true
fi

sched_ext_available=false
if [[ -d /sys/kernel/sched_ext ]]; then
    sched_ext_available=true
fi

cgroup2_mounted=false
if mount | grep -q 'type cgroup2'; then
    cgroup2_mounted=true
fi

cat >"$OUT" <<JSON
{
  "generated_at": "$(date -Is)",
  "host": $(hostname | json_escape),
  "os": $(printf '%s' "$os_pretty" | json_escape),
  "kernel": $(uname -r | json_escape),
  "cgroup_v2_mounted": $cgroup2_mounted,
  "btf_vmlinux": $(bool_file_readable /sys/kernel/btf/vmlinux),
  "lsm_list": $(printf '%s' "$lsm_list" | json_escape),
  "bpf_lsm_available": $bpf_lsm_available,
  "sched_ext_available": $sched_ext_available,
  "tools": {
    "clang": $(tool_path clang | json_escape),
    "bpftool": $(tool_path bpftool | json_escape),
    "tc": $(tool_path tc | json_escape),
    "ip": $(tool_path ip | json_escape),
    "kubectl": $(tool_path kubectl | json_escape)
  },
  "kernel_config": {
    "CONFIG_BPF": $(kernel_config_value CONFIG_BPF | json_escape),
    "CONFIG_BPF_SYSCALL": $(kernel_config_value CONFIG_BPF_SYSCALL | json_escape),
    "CONFIG_DEBUG_INFO_BTF": $(kernel_config_value CONFIG_DEBUG_INFO_BTF | json_escape),
    "CONFIG_BPF_LSM": $(kernel_config_value CONFIG_BPF_LSM | json_escape),
    "CONFIG_SCHED_CLASS_EXT": $(kernel_config_value CONFIG_SCHED_CLASS_EXT | json_escape)
  },
  "sp3_compatibility_policy": {
    "cgroup_v2": "required",
    "safe_doctor": "required",
    "network_security_supported_hooks": "smoke if capability is present; otherwise capability detection must report unavailable",
    "sched_ext": "graceful fallback required when unavailable on official kernel"
  },
  "high_risk_defaults": {
    "external_cgroup_write": "disabled",
    "memory_reclaim": "disabled",
    "xdp_dangerous_netdev": "disabled",
    "tc_existing_qdisc": "disabled",
    "doctor_live_probe": "disabled"
  }
}
JSON

echo "capability_matrix=$OUT"
