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
    if ! grep -q "$reason" "$TMP_DIR/out.log"; then
        echo "invalid config did not report expected reason: $reason" >&2
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
scheduler:
  type: cgroup_v2
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
unknown_top_level: true
'
expect_invalid "$unknown_yaml" "unknown config field: root.unknown_top_level"

bad_mode_yaml="$TMP_DIR/bad-mode-agent.yaml"
write_agent_yaml "$bad_mode_yaml" '
agent:
  name: EulerPilot
  mode: maybe
  interval_ms: 1000
'
expect_invalid "$bad_mode_yaml" "unknown agent.mode in config: maybe"

bad_interval_yaml="$TMP_DIR/bad-interval-agent.yaml"
write_agent_yaml "$bad_interval_yaml" '
agent:
  name: EulerPilot
  mode: dry-run
  interval_ms: 0
'
expect_invalid "$bad_interval_yaml" "config field out of range: agent.interval_ms"

unused_agent_field_yaml="$TMP_DIR/unused-agent-field.yaml"
write_agent_yaml "$unused_agent_field_yaml" '
agent:
  name: EulerPilot
  mode: dry-run
  interval_ms: 1000
  fallback_enabled: true
'
expect_invalid "$unused_agent_field_yaml" "unknown config field: agent.fallback_enabled"

unused_observer_field_yaml="$TMP_DIR/unused-observer-field.yaml"
write_agent_yaml "$unused_observer_field_yaml" '
agent:
  name: EulerPilot
  mode: dry-run
  interval_ms: 1000
observer:
  ebpf:
    enabled: true
'
expect_invalid "$unused_observer_field_yaml" "unknown config field: observer.ebpf.enabled"

unused_observer_collect_yaml="$TMP_DIR/unused-observer-collect-field.yaml"
write_agent_yaml "$unused_observer_collect_yaml" '
agent:
  name: EulerPilot
  mode: dry-run
  interval_ms: 1000
observer:
  ebpf:
    collect_wait_ns: true
'
expect_invalid "$unused_observer_collect_yaml" "unknown config field: observer.ebpf.collect_wait_ns"

unused_scheduler_field_yaml="$TMP_DIR/unused-scheduler-field.yaml"
write_agent_yaml "$unused_scheduler_field_yaml" '
agent:
  name: EulerPilot
  mode: dry-run
  interval_ms: 1000
scheduler:
  type: cgroup_v2
  default_profile: normal_profile
'
expect_invalid "$unused_scheduler_field_yaml" "unknown config field: scheduler.default_profile"

unused_scheduler_name_yaml="$TMP_DIR/unused-scheduler-name.yaml"
write_agent_yaml "$unused_scheduler_name_yaml" '
agent:
  name: EulerPilot
  mode: dry-run
  interval_ms: 1000
scheduler:
  type: cgroup_v2
  name: eulerpilot
'
expect_invalid "$unused_scheduler_name_yaml" "unknown config field: scheduler.name"

unused_scheduler_rollback_yaml="$TMP_DIR/unused-scheduler-rollback.yaml"
write_agent_yaml "$unused_scheduler_rollback_yaml" '
agent:
  name: EulerPilot
  mode: dry-run
  interval_ms: 1000
scheduler:
  type: cgroup_v2
  enable_rollback: true
'
expect_invalid "$unused_scheduler_rollback_yaml" "unknown config field: scheduler.enable_rollback"

echo "PASS: config validation rejects unknown fields, unused reserved fields, bad modes and bad ranges"
