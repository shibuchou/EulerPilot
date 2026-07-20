#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="${ROOT:-$(cd "$SCRIPT_DIR/../.." && pwd)}"
AGENT_BIN="$ROOT/build/eulerpilot-agent"
AGENT_YAML="$ROOT/configs/agent.yaml"
SKILLS_YAML="$ROOT/configs/skills.yaml"
DEMO_CGROUP="/sys/fs/cgroup/eulerpilot/demo-net"
PORT=18080
RESULT_DIR="$ROOT/results/smoke/network-policy-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$RESULT_DIR"

echo "=== Network Policy Demo Smoke Test ===" | tee "$RESULT_DIR/smoke.log"

# 1. Enable only network_policy_demo
echo "[1/5] Enabling network_policy_demo, disabling others..." | tee -a "$RESULT_DIR/smoke.log"
cp "$SKILLS_YAML" "$RESULT_DIR/skills.yaml.bak"
python3 -c "
import yaml
with open('$SKILLS_YAML') as f:
    cfg = yaml.safe_load(f)
for s in cfg['skills']:
    if s['name'] == 'network_policy_demo':
        s['enabled'] = True
    else:
        s['enabled'] = False
with open('$SKILLS_YAML', 'w') as f:
    yaml.dump(cfg, f, default_flow_style=False)
"
echo "  Done." | tee -a "$RESULT_DIR/smoke.log"

# 2. Start HTTP server
echo "[2/5] Starting HTTP server on port $PORT..." | tee -a "$RESULT_DIR/smoke.log"
pkill -f "http.server $PORT" 2>/dev/null || true
python3 -m http.server $PORT --bind 127.0.0.1 &>/tmp/http_server.log &
HTTP_PID=$!
sleep 1
if ! kill -0 $HTTP_PID 2>/dev/null; then
    echo "  FAIL: HTTP server did not start" | tee -a "$RESULT_DIR/smoke.log"
    exit 1
fi
echo "  PID=$HTTP_PID" | tee -a "$RESULT_DIR/smoke.log"

# 3. Verify HTTP works before attach
echo "[3/5] Verify HTTP before BPF attach..." | tee -a "$RESULT_DIR/smoke.log"
HTTP_CODE=$(curl -s -o /dev/null -w '%{http_code}' http://127.0.0.1:$PORT/ 2>/dev/null || echo '000')
echo "  HTTP code: $HTTP_CODE" | tee -a "$RESULT_DIR/smoke.log"

# 4. Run agent briefly to attach BPF
echo "[4/5] Running agent to attach BPF..." | tee -a "$RESULT_DIR/smoke.log"
timeout 8s "$AGENT_BIN" --config "$AGENT_YAML" --dry-run --duration 5 --interval-ms 1000 2>&1 | tee "$RESULT_DIR/agent_output.txt" || true
sleep 1

# 5. Test deny
echo "[5/5] Testing deny from demo-net cgroup..." | tee -a "$RESULT_DIR/smoke.log"
if [ -d "$DEMO_CGROUP" ]; then
    echo "  BPF programs on demo-net:" | tee -a "$RESULT_DIR/smoke.log"
    bpftool cgroup show "$DEMO_CGROUP" 2>&1 | tee -a "$RESULT_DIR/smoke.log"
    
    # Run from inside demo-net - use a subshell
    MYSELF=$$
    echo "$MYSELF" > "$DEMO_CGROUP/cgroup.procs" 2>/dev/null
    DENY_CODE=$(curl -s -o /dev/null -w '%{http_code}' --max-time 3 http://127.0.0.1:$PORT/ 2>/dev/null || echo '000')
    echo "  HTTP code (inside demo-net): $DENY_CODE" | tee -a "$RESULT_DIR/smoke.log"
    
    if [ "$DENY_CODE" = "000" ]; then
        echo "  PASS: Connection was DENIED" | tee -a "$RESULT_DIR/smoke.log"
    else
        echo "  FAIL: Expected deny, got $DENY_CODE" | tee -a "$RESULT_DIR/smoke.log"
    fi
else
    echo "  FAIL: demo-net cgroup not found" | tee -a "$RESULT_DIR/smoke.log"
fi

# Restore
echo "Restoring skills.yaml..." | tee -a "$RESULT_DIR/smoke.log"
cp "$RESULT_DIR/skills.yaml.bak" "$SKILLS_YAML"
pkill -f "http.server $PORT" 2>/dev/null || true
bash "$ROOT/scripts/cleanup_network_policy_demo.sh" 2>/dev/null || true
echo "=== Smoke test complete: $RESULT_DIR ===" | tee -a "$RESULT_DIR/smoke.log"
