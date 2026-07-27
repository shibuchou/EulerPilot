#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd -P)"
AGENT_BIN="$ROOT_DIR/build/eulerpilot-agent"
TMP_DIR="$(mktemp -d)"

cleanup() {
    rm -rf "$TMP_DIR"
}
trap cleanup EXIT

write_agent_yaml() {
    local config_path="$1"
    local skills_path="$2"
    cat >"$config_path" <<YAML
agent:
  name: EulerPilot
  mode: dry-run
  interval_ms: 1000
skills_config_path: "$skills_path"
scheduler:
  type: cgroup_v2
exporter:
  prometheus:
    enabled: false
    listen: 127.0.0.1:9108
YAML
}

expect_valid() {
    local config_path="$1"
    "$AGENT_BIN" --validate-config "$config_path" >"$TMP_DIR/valid.out" 2>&1
}

expect_invalid() {
    local config_path="$1"
    local expected="$2"
    if "$AGENT_BIN" --validate-config "$config_path" >"$TMP_DIR/invalid.out" 2>&1; then
        echo "expected invalid config containing: $expected" >&2
        cat "$TMP_DIR/invalid.out" >&2
        exit 1
    fi
    if ! grep -q "$expected" "$TMP_DIR/invalid.out"; then
        echo "invalid config did not report expected reason: $expected" >&2
        cat "$TMP_DIR/invalid.out" >&2
        exit 1
    fi
}

make -C "$ROOT_DIR" agent >/dev/null

audit_skills="$TMP_DIR/audit-skills.yaml"
cat >"$audit_skills" <<'YAML'
schema_version: 2
skills:
- name: security_policy
  kind: runtime
  enabled: true
  config:
    mode: audit
    targets:
      sensitive_path:
        type: path
        path: /etc/passwd
    rules:
      - name: audit_sensitive_path
        hook: lsm_file_open
        target_ref: sensitive_path
        mode: audit
        action: deny
YAML
audit_agent="$TMP_DIR/audit-agent.yaml"
write_agent_yaml "$audit_agent" "$audit_skills"
expect_valid "$audit_agent"

enforce_skills="$TMP_DIR/enforce-skills.yaml"
cat >"$enforce_skills" <<'YAML'
schema_version: 2
skills:
- name: security_policy
  kind: runtime
  enabled: true
  config:
    mode: enforce
    targets:
      sensitive_path:
        type: path
        path: /etc/passwd
    rules:
      - name: deny_sensitive_path
        hook: lsm_file_open
        target_ref: sensitive_path
        mode: enforce
        action: deny
YAML
enforce_agent="$TMP_DIR/enforce-agent.yaml"
write_agent_yaml "$enforce_agent" "$enforce_skills"
expect_invalid "$enforce_agent" "security-policy-enforce-rule-scope-missing"

socket_skills="$TMP_DIR/socket-skills.yaml"
cat >"$socket_skills" <<'YAML'
schema_version: 2
skills:
- name: security_policy
  kind: runtime
  enabled: true
  config:
    mode: audit
    targets:
      udp_connect:
        type: path
        dst_ip: 127.0.0.1
        dst_port: '18080'
        protocol: udp
    rules:
      - name: audit_udp_socket
        hook: lsm_socket_connect
        target_ref: udp_connect
        mode: audit
        action: deny
YAML
socket_agent="$TMP_DIR/socket-agent.yaml"
write_agent_yaml "$socket_agent" "$socket_skills"
expect_invalid "$socket_agent" "security-policy-v2-socket-protocol-unavailable"

tcp_audit_skills="$TMP_DIR/tcp-audit-skills.yaml"
cat >"$tcp_audit_skills" <<'YAML'
schema_version: 2
skills:
- name: security_policy
  kind: runtime
  enabled: true
  config:
    mode: audit
    targets:
      tcp_connect:
        type: path
        dst_ip: 127.0.0.1
        dst_port: '18080'
        protocol: tcp
    rules:
      - name: audit_tcp_socket
        hook: lsm_socket_connect
        target_ref: tcp_connect
        mode: audit
        action: deny
YAML
tcp_audit_agent="$TMP_DIR/tcp-audit-agent.yaml"
write_agent_yaml "$tcp_audit_agent" "$tcp_audit_skills"
expect_valid "$tcp_audit_agent"

tcp_enforce_skills="$TMP_DIR/tcp-enforce-skills.yaml"
cat >"$tcp_enforce_skills" <<'YAML'
schema_version: 2
skills:
- name: security_policy
  kind: runtime
  enabled: true
  config:
    mode: enforce
    targets:
      tcp_connect:
        type: path
        dst_ip: 127.0.0.1
        dst_port: '18080'
        protocol: tcp
    rules:
      - name: deny_tcp_socket_without_scope
        hook: lsm_socket_connect
        target_ref: tcp_connect
        mode: enforce
        action: deny
YAML
tcp_enforce_agent="$TMP_DIR/tcp-enforce-agent.yaml"
write_agent_yaml "$tcp_enforce_agent" "$tcp_enforce_skills"
expect_invalid "$tcp_enforce_agent" "security-policy-enforce-rule-scope-missing"

if ! grep -q 'sk_protocol' "$ROOT_DIR/bpf/security_policy.bpf.c"; then
    echo "security_policy.bpf.c must read socket protocol from struct sock" >&2
    exit 1
fi

if ! grep -q 'protocol == 0' "$ROOT_DIR/bpf/security_policy.bpf.c"; then
    echo "security_policy.bpf.c must fail closed when socket protocol is unavailable" >&2
    exit 1
fi

echo "PASS: security policy fail-closed scope and protocol validation"
