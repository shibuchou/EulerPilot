#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
AGENT_BIN="$ROOT_DIR/build/eulerpilot-agent"
TMP_DIR="$(mktemp -d)"

cleanup() {
    rm -rf "$TMP_DIR"
}
trap cleanup EXIT

write_agent_yaml() {
    local path="$1"
    local body="$2"
    cat >"$path" <<YAML
$body
skills_config_path: "$ROOT_DIR/configs/skills.yaml"
YAML
}

expect_valid() {
    local config="$1"
    "$AGENT_BIN" --validate-config "$config" >/dev/null
}

expect_invalid() {
    local config="$1"
    local reason="$2"
    if "$AGENT_BIN" --validate-config "$config" >"$TMP_DIR/out.log" 2>&1; then
        echo "expected invalid config: $reason" >&2
        cat "$TMP_DIR/out.log" >&2
        exit 1
    fi
}

make agent >/dev/null

valid_yaml="$TMP_DIR/valid-agent.yaml"
write_agent_yaml "$valid_yaml" '
agent:
  name: EulerPilot
  mode: dry-run
  interval_ms: 250
  fallback_enabled: true
observer:
  ebpf:
    enabled: true
    collect_sched: true
    collect_cgroup: true
    collect_migration: true
    collect_psi: true
scheduler:
  type: cgroup_v2
  name: cgroup_executor
  default_profile: normal_profile
  enable_rollback: true
exporter:
  prometheus:
    enabled: false
    listen: 127.0.0.1:9108
'
expect_valid "$valid_yaml"

unknown_yaml="$TMP_DIR/unknown-agent.yaml"
write_agent_yaml "$unknown_yaml" '
agent:
  name: EulerPilot
  mode: dry-run
  interval_ms: 1000
  fallback_enabled: true
unknown_top_level: true
'
expect_invalid "$unknown_yaml" "unknown top-level field"

bad_mode_yaml="$TMP_DIR/bad-mode-agent.yaml"
write_agent_yaml "$bad_mode_yaml" '
agent:
  name: EulerPilot
  mode: maybe
  interval_ms: 1000
  fallback_enabled: true
'
expect_invalid "$bad_mode_yaml" "invalid agent.mode"

bad_interval_yaml="$TMP_DIR/bad-interval-agent.yaml"
write_agent_yaml "$bad_interval_yaml" '
agent:
  name: EulerPilot
  mode: dry-run
  interval_ms: 0
  fallback_enabled: true
'
expect_invalid "$bad_interval_yaml" "invalid interval_ms"

echo "PASS: config validation rejects unknown fields, bad modes and bad ranges"
