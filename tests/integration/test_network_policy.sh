#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

AGENT_BIN="build/eulerpilot-agent"
AGENT_YAML="configs/agent.yaml"
SKILLS_YAML="configs/skills.yaml"
RESULT_DIR="results/network_policy/integration-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$RESULT_DIR"

log() {
    echo "$*" | tee -a "$RESULT_DIR/test.log"
}

restore() {
    if [ -f "$RESULT_DIR/skills.yaml.bak" ]; then
        cp "$RESULT_DIR/skills.yaml.bak" "$SKILLS_YAML"
    fi
    bash scripts/cleanup_network_policy_demo.sh >/dev/null 2>&1 || true
}
trap restore EXIT

log "=== NetworkPolicySkill integration test ==="
cp "$SKILLS_YAML" "$RESULT_DIR/skills.yaml.bak"

if ! "$AGENT_BIN" --list-skills | grep -qx "network_policy"; then
    log "FAIL: formal network_policy skill is not registered"
    exit 1
fi
log "PASS: formal network_policy skill is registered"

python3 - <<'PY'
from pathlib import Path

path = Path("configs/skills.yaml")
lines = path.read_text().splitlines()

prefix = []
blocks = []
current = []
in_skills = False
for line in lines:
    if line.startswith("skills:"):
        in_skills = True
        prefix.append(line)
        continue
    if in_skills and line.startswith("- "):
        if current:
            blocks.append(current)
        current = [line]
        continue
    if in_skills:
        current.append(line)
    else:
        prefix.append(line)
if current:
    blocks.append(current)

seen = False
out = prefix[:]
for block in blocks:
    name = ""
    for item in block:
        stripped = item.strip()
        if stripped.startswith("name:"):
            name = stripped.split(":", 1)[1].strip().strip("'\"")
            break

    if name == "network_policy":
        seen = True

    new_block = []
    for item in block:
        stripped = item.strip()
        indent = item[:len(item) - len(item.lstrip())]
        if stripped.startswith("enabled:"):
            enabled = "true" if name == "network_policy" else "false" if name in ("network_policy_demo", "security_policy_demo") else stripped.split(":", 1)[1].strip()
            new_block.append(f"{indent}enabled: {enabled}")
        elif name == "network_policy" and stripped.startswith("mode:"):
            new_block.append(f"{indent}mode: audit")
        else:
            new_block.append(item)
    out.extend(new_block)

if not seen:
    raise SystemExit("network_policy missing from skills.yaml")
path.write_text("\n".join(out) + "\n")
PY

timeout 20s "$AGENT_BIN" --doctor-skills --config "$AGENT_YAML" > "$RESULT_DIR/doctor.log" 2>&1
log "PASS: doctor succeeds with network_policy audit mode"

timeout 20s "$AGENT_BIN" --config "$AGENT_YAML" --duration-s 1 --interval-ms 1000 > "$RESULT_DIR/agent.log" 2>&1
log "PASS: agent runs with network_policy audit mode"

if [ -d /sys/fs/cgroup/eulerpilot/demo-net ] &&
   bpftool cgroup show /sys/fs/cgroup/eulerpilot/demo-net 2>/dev/null | grep -q .; then
    log "FAIL: audit mode attached cgroup BPF unexpectedly"
    exit 1
fi
log "PASS: audit mode has no cgroup BPF attachment"

if [ ! -s reports/events/network_policy.jsonl ]; then
    log "FAIL: expected network policy audit event"
    exit 1
fi
log "PASS: network policy audit event written"

log "=== NetworkPolicySkill integration test complete ==="
