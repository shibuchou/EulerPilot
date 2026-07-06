#!/usr/bin/env bash
set -euo pipefail
# EulerPilot Final Quality Gate — TAP-style
# P0: 22 blocking checks. P1: optional checks (not in TAP count).
# Run on 121. For 122: minimal regression only.

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

SKILLS_YAML="configs/skills.yaml"
AGENT_YAML="configs/agent.yaml"
AGENT_BIN="build/eulerpilot-agent"
TMPLOG="/tmp/eulerpilot-quality-gate.tmp"

TOTAL=22
N=1

echo "1..$TOTAL"

ok() {
    echo "ok $N - $1"
    N=$((N+1))
}

not_ok() {
    echo "not ok $N - $1"
    exit 1
}

run_silent() {
    "$@" > "$TMPLOG" 2>&1
}

ensure_skill_disabled() {
    local sk="$1"
    python3 -c "
lines = open('$SKILLS_YAML').read().splitlines()
block = -1
name_map = {}
enabled_map = {}
for line in lines:
    if line.startswith('- '):
        block += 1
    if block < 0:
        continue
    t = line.strip()
    if line.startswith('- name:'):
        name_map[block] = line.split(':', 1)[1].strip()
    elif line.startswith('  name:'):
        name_map[block] = t.split(':', 1)[1].strip()
    if line.startswith('  enabled:'):
        enabled_map[block] = t.split(':', 1)[1].strip()
# Find the block containing the target skill
for b, n in name_map.items():
    if n == '$sk':
        val = enabled_map.get(b, 'not-found')
        exit(0 if val == 'false' else 1)
exit(1)
" 2>/dev/null
}

# ============================================================
# P0 blocking checks
# ============================================================

# 1. make agent
if run_silent make agent; then
    ok "make agent"
else
    not_ok "make agent"
fi

# 2. make network-policy
if run_silent make network-policy; then
    ok "make network-policy"
else
    not_ok "make network-policy"
fi

# 3. make network-qos-tc
if run_silent make network-qos-tc; then
    ok "make network-qos-tc"
else
    not_ok "make network-qos-tc"
fi

# 4. make network-xdp
if run_silent make network-xdp; then
    ok "make network-xdp"
else
    not_ok "make network-xdp"
fi

# 5. make security-policy
if run_silent make security-policy; then
    ok "make security-policy"
else
    not_ok "make security-policy"
fi

# 6. C++ unit tests
if run_silent make unit-tests; then
    ok "make unit-tests"
else
    cat "$TMPLOG" >&2
    not_ok "make unit-tests"
fi

# 7. list skills
SKILLS_OUT=$("$AGENT_BIN" --list-skills 2>/dev/null)
SKILL_COUNT=$(echo "$SKILLS_OUT" | wc -l)
if [ "$SKILL_COUNT" -ge 8 ] &&
   echo "$SKILLS_OUT" | grep -q '^network_policy$' &&
   echo "$SKILLS_OUT" | grep -q '^network_qos$' &&
   echo "$SKILLS_OUT" | grep -q '^network_xdp$' &&
   echo "$SKILLS_OUT" | grep -q '^security_policy$'; then
    ok "--list-skills outputs formal network and security policy skills"
else
    not_ok "--list-skills missing formal network/security policy skill (count=$SKILL_COUNT)"
fi

# 8. doctor skills
if timeout 15s "$AGENT_BIN" --doctor-skills --config "$AGENT_YAML" > "$TMPLOG" 2>&1; then
    ok "--doctor-skills exit 0"
else
    cat "$TMPLOG" >&2
    not_ok "--doctor-skills failed"
fi

# 9. agent 15s smoke (metrics default closed)
echo "  metrics config:" "$(grep -A4 'prometheus:' "$AGENT_YAML" | head -4)"
if timeout 25s "$AGENT_BIN" --config "$AGENT_YAML" --duration-s 10 --interval-ms 2000 > "$TMPLOG" 2>&1; then
    ok "agent 15s smoke"
else
    cat "$TMPLOG" >&2
    not_ok "agent 15s smoke agent failed"
fi

# 10. network_policy disabled
if ensure_skill_disabled "network_policy"; then
    ok "network_policy default disabled"
else
    not_ok "network_policy not disabled"
fi

# 11. network_qos disabled
if ensure_skill_disabled "network_qos"; then
    ok "network_qos default disabled"
else
    not_ok "network_qos not disabled"
fi

# 12. network_xdp disabled
if ensure_skill_disabled "network_xdp"; then
    ok "network_xdp default disabled"
else
    not_ok "network_xdp not disabled"
fi

# 13. security_policy disabled
if ensure_skill_disabled "security_policy"; then
    ok "security_policy default disabled"
else
    not_ok "security_policy not disabled"
fi

# 14. security_policy_demo disabled
if ensure_skill_disabled "security_policy_demo"; then
    ok "security_policy_demo default disabled"
else
    not_ok "security_policy_demo not disabled"
fi

# 15. metrics default off + 127.0.0.1
if grep -A4 'prometheus:' "$AGENT_YAML" | grep -q 'enabled: false'; then
    if grep -A4 'prometheus:' "$AGENT_YAML" | grep -q '127.0.0.1'; then
        ok "metrics default disabled on 127.0.0.1"
    else
        not_ok "metrics listen not 127.0.0.1"
    fi
else
    not_ok "metrics not default disabled"
fi

# 16. dashboard exists
if [ -s "reports/dashboard/index.html" ]; then
    ok "dashboard index.html exists and non-empty"
else
    not_ok "dashboard index.html missing or empty"
fi

# 17. frozen result dirs
REDIS_DIRS=$(find results/final/ -maxdepth 1 -type d -name 'redis-*' 2>/dev/null | wc -l)
NGINX_DIRS=$(find results/final/ -maxdepth 1 -type d -name 'nginx-*' 2>/dev/null | wc -l)
if [ "$REDIS_DIRS" -ge 1 ] && [ "$NGINX_DIRS" -ge 1 ]; then
    ok "frozen result dirs exist (Redis=$REDIS_DIRS, Nginx=$NGINX_DIRS)"
else
    not_ok "frozen result dirs missing (Redis=$REDIS_DIRS, Nginx=$NGINX_DIRS)"
fi

# 18. resource_control CPU+Memory+IO evidence
RESOURCE_CONTROL_OK=true
for summary in \
    results/resource_control/integration-20260624-160317/summary.txt \
    results/resource_control/integration-20260624-160349/summary.txt \
    results/resource_control/io-20260624-160008/summary.txt \
    results/resource_control/io-20260624-160208/summary.txt; do
    if [ ! -s "$summary" ] || ! grep -q '^result=pass$' "$summary"; then
        echo "  ERROR: missing or failed resource control summary: $summary"
        RESOURCE_CONTROL_OK=false
    fi
done
if ! grep -q '^cpu_max_pressure=10000 100000$' results/resource_control/integration-20260624-160317/summary.txt; then
    echo "  ERROR: 121 CPU pressure evidence missing"
    RESOURCE_CONTROL_OK=false
fi
if ! grep -q '^memory_high_pressure=1048576$' results/resource_control/integration-20260624-160349/summary.txt; then
    echo "  ERROR: 122 memory pressure evidence missing"
    RESOURCE_CONTROL_OK=false
fi
for summary in \
    results/resource_control/io-20260624-160008/summary.txt \
    results/resource_control/io-20260624-160208/summary.txt; do
    if ! grep -q '^io_max_pressure=.*wbps=1048576$' "$summary"; then
        echo "  ERROR: IO max pressure evidence missing in $summary"
        RESOURCE_CONTROL_OK=false
    fi
    if ! grep -q '^io_weight_pressure=default 50$' "$summary"; then
        echo "  ERROR: IO weight pressure evidence missing in $summary"
        RESOURCE_CONTROL_OK=false
    fi
done
if $RESOURCE_CONTROL_OK; then
    ok "resource_control CPU+Memory+IO evidence"
else
    not_ok "resource_control CPU+Memory+IO evidence missing"
fi

# 18. resource_control target_ref evidence
RESOURCE_TARGET_OK=true
for summary in \
    results/resource_control/target-20260624-172139/summary.txt \
    results/resource_control/target-20260624-172916/summary.txt; do
    if [ ! -s "$summary" ] ||
       ! grep -q '^result=pass$' "$summary" ||
       ! grep -q '^target_ref=background_scope$' "$summary" ||
       ! grep -q '^target_cgroup=/sys/fs/cgroup/eulerpilot/target-background$' "$summary" ||
       ! grep -q '^cpu_max_pressure=10000 100000$' "$summary" ||
       ! grep -q '^memory_high_pressure=1048576$' "$summary" ||
       ! grep -q '^outside_cpu_max=max 100000$' "$summary"; then
        echo "  ERROR: missing target_ref evidence in $summary"
        RESOURCE_TARGET_OK=false
    fi
done
if $RESOURCE_TARGET_OK; then
    ok "resource_control target_ref evidence"
else
    not_ok "resource_control target_ref evidence missing"
fi

# 19. resource_control runtime target evidence
RESOURCE_RUNTIME_TARGET_OK=true
for summary in \
    results/resource_control/runtime-target-20260624-212403/summary.txt \
    results/resource_control/runtime-target-20260624-212529/summary.txt; do
    if [ ! -s "$summary" ] ||
       ! grep -q '^result=pass$' "$summary" ||
       ! grep -q '^target_types=container_id,container,k8s_pod$' "$summary" ||
       ! grep -q '^container_id_target_ref=container_id_scope$' "$summary" ||
       ! grep -q '^container_name_target_ref=container_name_scope$' "$summary" ||
       ! grep -q '^k8s_pod_target_ref=pod_scope$' "$summary" ||
       ! grep -q '^container_id_cpu_max_pressure=10000 100000$' "$summary" ||
       ! grep -q '^container_name_cpu_max_pressure=10000 100000$' "$summary" ||
       ! grep -q '^k8s_pod_cpu_max_pressure=10000 100000$' "$summary" ||
       ! grep -q '^container_id_memory_high_pressure=1048576$' "$summary" ||
       ! grep -q '^container_name_memory_high_pressure=1048576$' "$summary" ||
       ! grep -q '^k8s_pod_memory_high_pressure=1048576$' "$summary"; then
        echo "  ERROR: missing runtime target evidence in $summary"
        RESOURCE_RUNTIME_TARGET_OK=false
    fi
done
if $RESOURCE_RUNTIME_TARGET_OK; then
    ok "resource_control runtime target evidence"
else
    not_ok "resource_control runtime target evidence missing"
fi

# 20. resource_control CPU quota effect evidence
RESOURCE_CPU_QUOTA_OK=true
for summary in \
    results/resource_control/cpu-quota-20260625-095030/summary.txt \
    results/resource_control/cpu-quota-20260625-095114/summary.txt; do
    if [ ! -s "$summary" ] ||
       ! grep -q '^result=pass$' "$summary" ||
       ! grep -q '^cpu_max_pressure=10000 100000$' "$summary"; then
        echo "  ERROR: missing CPU quota summary fields in $summary"
        RESOURCE_CPU_QUOTA_OK=false
        continue
    fi

    ratio=$(awk -F= '$1 == "usage_rate_ratio" { print $2 }' "$summary")
    throttled=$(awk -F= '$1 == "limited_nr_throttled_delta" { print $2 }' "$summary")
    throttled_usec=$(awk -F= '$1 == "limited_throttled_usec_delta" { print $2 }' "$summary")
    if ! awk -v ratio="$ratio" -v throttled="$throttled" -v throttled_usec="$throttled_usec" \
        'BEGIN { exit !(ratio > 0 && ratio < 0.70 && throttled > 0 && throttled_usec > 0) }'; then
        echo "  ERROR: weak CPU quota evidence in $summary ratio=$ratio throttled=$throttled throttled_usec=$throttled_usec"
        RESOURCE_CPU_QUOTA_OK=false
    fi
done
if $RESOURCE_CPU_QUOTA_OK; then
    ok "resource_control CPU quota effect evidence"
else
    not_ok "resource_control CPU quota effect evidence missing"
fi

# 21. no BPF/LSM/TC/XDP residue
RESIDUE_OK=true
if [ -e /sys/fs/bpf/security_policy_demo_link ]; then
    echo "  ERROR: /sys/fs/bpf/security_policy_demo_link still pinned"
    RESIDUE_OK=false
fi
if [ -d /sys/fs/cgroup/eulerpilot/demo-net ]; then
    if bpftool cgroup show /sys/fs/cgroup/eulerpilot/demo-net 2>/dev/null | grep -q .; then
        echo "  ERROR: demo-net cgroup has attached BPF program"
        RESIDUE_OK=false
    fi
fi
if ip link show ep-veth-qos0 >/dev/null 2>&1; then
    echo "  ERROR: ep-veth-qos0 still exists"
    RESIDUE_OK=false
fi
if ip link show ep-veth-xdp0 >/dev/null 2>&1; then
    echo "  ERROR: ep-veth-xdp0 still exists"
    RESIDUE_OK=false
fi
if $RESIDUE_OK; then
    ok "no BPF/LSM/TC/XDP residue"
else
    not_ok "BPF/LSM/TC/XDP residue found"
fi

# ============================================================
# P1: optional checks (informational, non-blocking)
# ============================================================
echo ""
echo "# optional checks"

# P1-1: agent 10-round stress smoke
STRESS_OK=true
for i in $(seq 1 100); do
    if ! timeout 25s "$AGENT_BIN" --config "$AGENT_YAML" --duration-s 5 --interval-ms 2000 > /tmp/eulerpilot-smoke-$i.log 2>&1; then
        echo "  FAIL round $i"
        STRESS_OK=false
        break
    fi
done
if $STRESS_OK; then
    echo "ok - agent 100-round stress smoke"
else
    echo "not ok - agent 100-round stress smoke (see /tmp/eulerpilot-smoke-*.log)"
fi

# P1-2: doctor 5-round
DOCTOR_OK=true
for i in $(seq 1 5); do
    if ! timeout 15s "$AGENT_BIN" --doctor-skills --config "$AGENT_YAML" > /tmp/eulerpilot-doctor-$i.log 2>&1; then
        echo "  FAIL round $i"
        DOCTOR_OK=false
        break
    fi
done
if $DOCTOR_OK; then
    echo "ok - doctor 5-round stable"
else
    echo "not ok - doctor 5-round unstable (see /tmp/eulerpilot-doctor-*.log)"
fi

echo ""
echo "quality gate complete"
