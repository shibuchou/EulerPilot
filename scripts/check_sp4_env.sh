#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

section() {
    printf '\n== %s ==\n' "$1"
}

section "OS"
if [[ -r /etc/os-release ]]; then
    cat /etc/os-release
else
    echo "/etc/os-release not found"
fi

section "Kernel"
uname -a

section "openEuler SP4 marker"
if [[ -r /etc/os-release ]] && grep -Eqi 'openEuler.*24\.03.*SP4|24\.03.*SP4' /etc/os-release; then
    echo "sp4_detected=yes"
else
    echo "sp4_detected=no"
    echo "note=SP4 is not required for v3.1; this check is preparatory."
fi

section "cgroup v2"
if mount | grep -q 'type cgroup2'; then
    echo "cgroup2=mounted"
    if [[ -r /sys/fs/cgroup/cgroup.controllers ]]; then
        echo -n "controllers="
        cat /sys/fs/cgroup/cgroup.controllers
    fi
else
    echo "cgroup2=missing"
fi

section "BPF/BTF"
if [[ -r /sys/kernel/btf/vmlinux ]]; then
    echo "btf=yes"
else
    echo "btf=no"
fi
command -v bpftool >/dev/null 2>&1 && bpftool version || echo "bpftool=missing"

section "Toolchain"
for tool in clang llc llvm-strip make gcc g++ tc ip; do
    if command -v "$tool" >/dev/null 2>&1; then
        printf '%s=' "$tool"
        command -v "$tool"
    else
        printf '%s=missing\n' "$tool"
    fi
done

section "Kernel config hints"
for cfg in CONFIG_BPF CONFIG_BPF_SYSCALL CONFIG_DEBUG_INFO_BTF CONFIG_BPF_LSM CONFIG_SCHED_CLASS_EXT; do
    if [[ -r /proc/config.gz ]]; then
        zgrep -E "^${cfg}=" /proc/config.gz || true
    elif [[ -r "/boot/config-$(uname -r)" ]]; then
        grep -E "^${cfg}=" "/boot/config-$(uname -r)" || true
    else
        echo "${cfg}=unknown"
    fi
done

section "EulerPilot config smoke"
if [[ -x "$ROOT_DIR/build/eulerpilot-agent" ]]; then
    "$ROOT_DIR/build/eulerpilot-agent" --validate-config "$ROOT_DIR/configs/agent.yaml" || true
else
    echo "agent=not_built"
fi