#!/usr/bin/env bash
# EulerPilot Release Candidate Demo Script
# --static: show existing results (default)
# --live:   run stable demo commands (no benchmarks)
set -euo pipefail

MODE="${1:-static}"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

echo "============================================"
echo "  EulerPilot Release Candidate Demo"
echo "  Mode: $MODE"
echo "============================================"

if [ "$MODE" = "--live" ]; then
  echo ""
  echo "[LIVE] Skills check"
  ./build/eulerpilot-agent --list-skills 2>/dev/null
  echo ""
  echo "[LIVE] Doctor check"
  ./build/eulerpilot-agent --doctor-skills 2>/dev/null
  echo ""
  echo "[LIVE] Agent 10s smoke"
  timeout 15s ./build/eulerpilot-agent --config configs/agent.yaml --duration-s 10 --interval-ms 2000 2>/dev/null || true
  echo ""
  echo "[LIVE] Rollback"
  bash scripts/rollback.sh 2>/dev/null || true
  echo ""
  echo "[LIVE] Quality gate (quick)"
  bash scripts/final_quality_gate.sh 2>/dev/null | head -20 || true
  echo ""
  echo "[LIVE] Demo results:"
  echo "  Redis:  $(ls results/final/redis-*/compare_summary_avg.csv 2>/dev/null | tail -1 || echo 'on 122')"
  echo "  Nginx:  $(ls results/final/nginx-*/compare_summary_avg.csv 2>/dev/null | tail -1 || echo 'on 122')"
  echo "  Dashboard: reports/dashboard/index.html"
  echo ""
  echo "============================================"
  echo "  Live demo complete."
  echo "============================================"
  exit 0
fi

echo ""
echo "[1/6] Version"
echo "  Git commit: $(git rev-parse --short HEAD 2>/dev/null || echo 'N/A')"
echo "  Tag:        $(git describe --tags --exact-match 2>/dev/null || echo 'N/A')"

echo ""
echo "[2/6] Registered Skills"
./build/eulerpilot-agent --list-skills 2>/dev/null

echo ""
echo "[3/6] Skills Availability (probe-only, no side effects)"
./build/eulerpilot-agent --doctor-skills --config configs/agent.yaml 2>/dev/null

echo ""
echo "[4/6] Redis Candidate Results"
REDIS_DIR="results/final/redis-scx-compare-20260612-191543"
if [ -d "$REDIS_DIR" ]; then
    echo "  Directory: $REDIS_DIR"
    echo "  Runs:      $(find "$REDIS_DIR" -maxdepth 1 -type d -name 'run-*' 2>/dev/null | wc -l)"
    [ -f "$REDIS_DIR/run_manifest.json" ] && echo "  Manifest:  yes" || echo "  Manifest:  no"
    [ -f "$REDIS_DIR/compare_summary_avg.csv" ] && echo "  CSV:       yes" || echo "  CSV:       no"
    [ -f "$REDIS_DIR/report.md" ] && echo "  Report:    yes" || echo "  Report:    no"
else
    echo "  (results on 122 OLK-6.6 machine)"
fi

echo ""
echo "[5/6] Nginx Candidate Results"
NGINX_DIR="results/final/nginx-scx-compare-20260612-194018"
if [ -d "$NGINX_DIR" ]; then
    echo "  Directory: $NGINX_DIR"
    echo "  Runs:      $(find "$NGINX_DIR" -maxdepth 1 -type d -name 'run-*' 2>/dev/null | wc -l)"
    [ -f "$NGINX_DIR/run_manifest.json" ] && echo "  Manifest:  yes" || echo "  Manifest:  no"
    [ -f "$NGINX_DIR/compare_summary_avg.csv" ] && echo "  CSV:       yes" || echo "  CSV:       no"
    [ -f "$NGINX_DIR/report.md" ] && echo "  Report:    yes" || echo "  Report:    no"
else
    echo "  (results on 122 OLK-6.6 machine)"
fi

echo ""
echo "[6/6] Figures"
FIGURES_DIR="reports/final_figures"
if [ -d "$FIGURES_DIR" ]; then
    echo "  Figures: $FIGURES_DIR/ ($(find "$FIGURES_DIR" -maxdepth 1 -name '*.svg' 2>/dev/null | wc -l) SVG)"
fi

echo ""
echo "[7/7] Dashboard"
DASHBOARD="reports/dashboard/index.html"
if [ -f "$DASHBOARD" ]; then
    SIZE=$(wc -c < "$DASHBOARD")
    echo "  File: $DASHBOARD (${SIZE} bytes)"
    echo "  Open: reports/dashboard/index.html in browser"
else
    echo "  Not found. Run: python3 scripts/build_dashboard.py"
fi

echo ""
echo "--- Manual Demo Commands (run on demand) ---"
echo "  dashboard:"
echo "    reports/dashboard/index.html  (open in browser, no server needed)"
echo ""
echo "  metrics (optional):"
echo "    # enable exporter.prometheus.enabled=true in configs/agent.yaml"
echo "    ./build/eulerpilot-agent --config configs/agent.yaml &"
echo "    curl http://127.0.0.1:9108/metrics"
echo ""
echo "  network_policy_demo:"
echo "    make network-policy"
echo "    # enable in configs/skills.yaml, then:"
echo "    ./build/eulerpilot-agent --config configs/agent.yaml"
echo "    # cgroup 内 curl -> 000 (deny), cgroup 外 -> 200"
echo ""
echo "  security_policy_demo:"
echo "    make security-policy"
echo "    # enable in configs/skills.yaml, then:"
echo "    ./build/eulerpilot-agent --config configs/agent.yaml"
echo "    # cat demo/security_policy_demo/secret.txt -> Operation not permitted"
echo ""
echo "============================================"
echo "  Demo complete. EulerPilot RC ready."
echo "============================================"
