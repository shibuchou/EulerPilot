#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

AGENT_BIN="${AGENT_BIN:-$ROOT/build/eulerpilot-agent}"
RESULT_DIR="${RESULT_DIR:-$ROOT/results/network_policy/cgroup-ownership-$(date +%Y%m%d-%H%M%S)}"
RUN_ID="np-own-$$"
EULER_ROOT="/sys/fs/cgroup/eulerpilot"
AUDIT_MISSING="$EULER_ROOT/${RUN_ID}-audit-missing"
AUDIT_EXISTING="$EULER_ROOT/${RUN_ID}-audit-existing"
EXTERNAL_CGROUP="/sys/fs/cgroup/${RUN_ID}-external"

mkdir -p "$RESULT_DIR"

cleanup() {
    rmdir "$AUDIT_EXISTING" 2>/dev/null || true
    rmdir "$EXTERNAL_CGROUP" 2>/dev/null || true
}
trap cleanup EXIT

write_config() {
    local mode="$1"
    local target_path="$2"
    local agent_yaml="$RESULT_DIR/agent-$mode.yaml"
    local skills_yaml="$RESULT_DIR/skills-$mode.yaml"
    cat > "$agent_yaml" <<EOF
agent:
  name: EulerPilot
  mode: dry-run
  interval_ms: 200
skills_config_path: $skills_yaml
scheduler:
  type: cgroup_v2
EOF
    cat > "$skills_yaml" <<EOF
schema_version: 2
skills:
- name: network_policy
  kind: runtime
  enabled: true
  config:
    mode: $mode
    targets:
      target:
        type: cgroup
        path: $target_path
    rules:
      - name: ownership_test
        hook: cgroup_connect4
        target_ref: target
        protocol: tcp
        dst_port: '18080'
        action: deny
EOF
    printf '%s\n' "$agent_yaml"
}

same_identity() {
    local before="$1"
    local path="$2"
    local after
    after="$(stat -Lc '%d:%i' "$path")"
    [ "$before" = "$after" ]
}

mkdir -p "$EULER_ROOT"

audit_missing_cfg="$(write_config audit "$AUDIT_MISSING")"
"$AGENT_BIN" --config "$audit_missing_cfg" --duration-s 1 --interval-ms 200 \
    > "$RESULT_DIR/audit-missing.log" 2>&1 || true
if [ -e "$AUDIT_MISSING" ]; then
    echo "FAIL: audit mode created missing target cgroup" >&2
    exit 1
fi

mkdir -p "$AUDIT_EXISTING"
audit_existing_identity="$(stat -Lc '%d:%i' "$AUDIT_EXISTING")"
audit_existing_cfg="$(write_config audit "$AUDIT_EXISTING")"
"$AGENT_BIN" --config "$audit_existing_cfg" --duration-s 1 --interval-ms 200 \
    > "$RESULT_DIR/audit-existing.log" 2>&1 || true
same_identity "$audit_existing_identity" "$AUDIT_EXISTING" || {
    echo "FAIL: audit mode changed existing cgroup identity" >&2
    exit 1
}
if [ -e /sys/fs/bpf/eulerpilot_network_policy_link ]; then
    echo "FAIL: audit mode pinned a cgroup BPF link" >&2
    exit 1
fi

mkdir -p "$EXTERNAL_CGROUP"
external_identity="$(stat -Lc '%d:%i' "$EXTERNAL_CGROUP")"
external_cfg="$(write_config enforce "$EXTERNAL_CGROUP")"
"$AGENT_BIN" --config "$external_cfg" --duration-s 1 --interval-ms 200 \
    > "$RESULT_DIR/external-enforce.log" 2>&1 || true
same_identity "$external_identity" "$EXTERNAL_CGROUP" || {
    echo "FAIL: external cgroup was deleted or replaced" >&2
    exit 1
}
if ! grep -q 'external-cgroup-readonly' "$RESULT_DIR/external-enforce.log"; then
    echo "FAIL: external cgroup enforce did not fail closed with external-cgroup-readonly" >&2
    exit 1
fi

cat > "$RESULT_DIR/summary.txt" <<EOF
result=pass
network_policy_ownership_tests=pass
network_policy_audit_no_side_effect=pass
external_cgroup_enforce=fail_closed
EOF

echo "PASS: NetworkPolicy cgroup ownership checks passed"
