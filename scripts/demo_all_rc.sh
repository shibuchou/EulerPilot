#!/usr/bin/env bash
# EulerPilot Release Candidate Demo Script
# 只展示已有成果，不修改任何配置，不启动实验
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

echo "============================================"
echo "  EulerPilot Release Candidate Demo"
echo "============================================"

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
echo "[6/6] Figures & Manual Demos"
FIGURES_DIR="reports/final_figures"
if [ -d "$FIGURES_DIR" ]; then
    echo "  Figures: $FIGURES_DIR/ ($(find "$FIGURES_DIR" -maxdepth 1 -name '*.svg' 2>/dev/null | wc -l) SVG)"
fi
echo ""
echo "--- Manual Demo Commands (run on demand) ---"
echo "  network_policy_demo:"
echo "    make network-policy-demo"
echo "    # enable in configs/skills.yaml, then:"
echo "    ./build/eulerpilot-agent --config configs/agent.yaml"
echo "    # cgroup 内 curl -> 000 (deny), cgroup 外 -> 200"
echo ""
echo "  security_policy_demo:"
echo "    make security-policy-demo"
echo "    # enable in configs/skills.yaml, then:"
echo "    ./build/eulerpilot-agent --config configs/agent.yaml"
echo "    # cat demo/security_policy_demo/secret.txt -> Operation not permitted"
echo ""
echo "============================================"
echo "  Demo complete. EulerPilot RC ready."
echo "============================================"
