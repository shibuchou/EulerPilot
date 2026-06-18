#!/usr/bin/env bash
set -euo pipefail
# EulerPilot Final Quality Gate — TAP-style
# P0: 12 blocking checks. P1: optional checks (not in TAP count).
# Run on 121. For 122: minimal regression only.

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

SKILLS_YAML="configs/skills.yaml"
AGENT_YAML="configs/agent.yaml"
AGENT_BIN="build/eulerpilot-agent"
TMPLOG="/tmp/eulerpilot-quality-gate.tmp"

TOTAL=14
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
    t = line.strip()
    if t.startswith('- config:') or t.startswith('- name:'):
        block += 1
    if t.startswith('name:'):
        name_map[block] = t.split(':', 1)[1].strip()
    if t.startswith('enabled:'):
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
# P0: 12 blocking checks
# ============================================================

# 1. make agent
if run_silent make agent; then
    ok "make agent"
else
    not_ok "make agent"
fi

# 2. make network-policy-demo
if run_silent make network-policy-demo; then
    ok "make network-policy-demo"
else
    not_ok "make network-policy-demo"
fi

# 3. make network-qos-tc
if run_silent make network-qos-tc; then
    ok "make network-qos-tc"
else
    not_ok "make network-qos-tc"
fi

# 4. make security-policy-demo
if run_silent make security-policy-demo; then
    ok "make security-policy-demo"
else
    not_ok "make security-policy-demo"
fi

# 5. list skills
SKILLS_OUT=$("$AGENT_BIN" --list-skills 2>/dev/null)
SKILL_COUNT=$(echo "$SKILLS_OUT" | wc -l)
if [ "$SKILL_COUNT" -ge 6 ] &&
   echo "$SKILLS_OUT" | grep -q '^network_policy$' &&
   echo "$SKILLS_OUT" | grep -q '^network_qos$'; then
    ok "--list-skills outputs formal network_policy/network_qos skills"
else
    not_ok "--list-skills missing formal network_policy/network_qos skill (count=$SKILL_COUNT)"
fi

# 6. doctor skills
if timeout 15s "$AGENT_BIN" --doctor-skills --config "$AGENT_YAML" > "$TMPLOG" 2>&1; then
    ok "--doctor-skills exit 0"
else
    cat "$TMPLOG" >&2
    not_ok "--doctor-skills failed"
fi

# 7. agent 15s smoke (metrics default closed)
echo "  metrics config:" "$(grep -A4 'prometheus:' "$AGENT_YAML" | head -4)"
if timeout 25s "$AGENT_BIN" --config "$AGENT_YAML" --duration-s 10 --interval-ms 2000 > "$TMPLOG" 2>&1; then
    ok "agent 15s smoke"
else
    cat "$TMPLOG" >&2
    not_ok "agent 15s smoke agent failed"
fi

# 8. network_policy disabled
if ensure_skill_disabled "network_policy"; then
    ok "network_policy default disabled"
else
    not_ok "network_policy not disabled"
fi

# 9. network_qos disabled
if ensure_skill_disabled "network_qos"; then
    ok "network_qos default disabled"
else
    not_ok "network_qos not disabled"
fi

# 10. security_policy_demo disabled
if ensure_skill_disabled "security_policy_demo"; then
    ok "security_policy_demo default disabled"
else
    not_ok "security_policy_demo not disabled"
fi

# 11. metrics default off + 127.0.0.1
if grep -A4 'prometheus:' "$AGENT_YAML" | grep -q 'enabled: false'; then
    if grep -A4 'prometheus:' "$AGENT_YAML" | grep -q '127.0.0.1'; then
        ok "metrics default disabled on 127.0.0.1"
    else
        not_ok "metrics listen not 127.0.0.1"
    fi
else
    not_ok "metrics not default disabled"
fi

# 12. dashboard exists
if [ -s "reports/dashboard/index.html" ]; then
    ok "dashboard index.html exists and non-empty"
else
    not_ok "dashboard index.html missing or empty"
fi

# 13. frozen result dirs
REDIS_DIRS=$(find results/final/ -maxdepth 1 -type d -name 'redis-*' 2>/dev/null | wc -l)
NGINX_DIRS=$(find results/final/ -maxdepth 1 -type d -name 'nginx-*' 2>/dev/null | wc -l)
if [ "$REDIS_DIRS" -ge 1 ] && [ "$NGINX_DIRS" -ge 1 ]; then
    ok "frozen result dirs exist (Redis=$REDIS_DIRS, Nginx=$NGINX_DIRS)"
else
    not_ok "frozen result dirs missing (Redis=$REDIS_DIRS, Nginx=$NGINX_DIRS)"
fi

# 14. no BPF/LSM/TC residue
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
if $RESIDUE_OK; then
    ok "no BPF/LSM/TC residue"
else
    not_ok "BPF/LSM/TC residue found"
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
