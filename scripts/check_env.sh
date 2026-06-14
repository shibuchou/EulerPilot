#!/usr/bin/env bash
set -u

ok() { printf '[OK] %s\n' "$*"; }
warn() { printf '[WARN] %s\n' "$*"; }
fail() { printf '[FAIL] %s\n' "$*"; }
have() { command -v "$1" >/dev/null 2>&1; }

printf 'EulerPilot environment check\n'
printf '============================\n'

printf 'Host: '; hostname
printf 'Kernel: '; uname -r
if [ -f /etc/openEuler-release ]; then
    printf 'OS: '; cat /etc/openEuler-release
else
    warn '/etc/openEuler-release not found'
fi

for tool in gcc g++ clang make git bpftool llvm-strip readelf; do
    if have "$tool"; then
        ok "$tool: $(command -v "$tool")"
    else
        warn "$tool not found"
    fi
done

if pkg-config --exists yaml-cpp 2>/dev/null; then
    ok 'yaml-cpp is available'
else
    warn 'yaml-cpp is not available'
fi

if [ -d /usr/include/bpf ]; then
    ok 'libbpf headers are installed'
else
    warn 'libbpf headers are not installed'
fi

if [ -r /sys/kernel/btf/vmlinux ]; then
    ok 'BTF vmlinux is available'
else
    warn 'BTF vmlinux is not readable at /sys/kernel/btf/vmlinux'
fi

if [ -r /proc/config.gz ]; then
    if zcat /proc/config.gz | grep -q '^CONFIG_SCHED_CLASS_EXT=y'; then
        ok 'CONFIG_SCHED_CLASS_EXT=y'
    else
        warn 'CONFIG_SCHED_CLASS_EXT is not enabled in /proc/config.gz'
    fi
else
    warn '/proc/config.gz not available; try checking /boot/config-$(uname -r)'
fi

if [ -d /sys/kernel/sched_ext ]; then
    ok '/sys/kernel/sched_ext exists'
else
    warn '/sys/kernel/sched_ext not found; sched_ext may be unavailable or not loaded'
fi

if [ -r /proc/pressure/cpu ]; then
    ok 'CPU PSI is available'
    sed 's/^/[PSI] /' /proc/pressure/cpu
else
    warn 'CPU PSI is not available'
fi

if mount | grep -q 'type cgroup2'; then
    ok 'cgroup v2 is mounted'
else
    warn 'cgroup v2 mount not detected'
fi

if [ -x ./build/workload_observer_dump ]; then
    ok 'workload observer dump tool is built'
else
    warn 'workload observer dump tool is not built yet; run make observer'
fi

printf '\nNext step: integrate the workload observer output into the Agent runtime.\n'
