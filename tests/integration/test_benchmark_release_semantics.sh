#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

python3 - "$ROOT_DIR" <<'PY'
import re
import sys
from pathlib import Path

root = Path(sys.argv[1])

def read(rel: str) -> str:
    return (root / rel).read_text(encoding="utf-8")

def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)

def case_block(text: str, label: str) -> str:
    pattern = rf"^[ \t]*{re.escape(label)}\)\n(?P<body>.*?)[ \t]*;;"
    match = re.search(pattern, text, flags=re.M | re.S)
    if not match:
        raise SystemExit(f"missing case block: {label}")
    return match.group("body")

def run_case_text(text: str) -> str:
    marker = "run_case() {"
    start = text.find(marker)
    if start < 0:
        raise SystemExit("missing run_case function")
    return text[start:]

redis_compare = read("bench/redis/run_redis_sched_ext_compare.sh")
nginx_compare = read("bench/nginx/run_nginx_sched_ext_compare.sh")
static_compare = read("bench/redis/run_static_vs_agent_compare.sh")
redis_run_case = run_case_text(redis_compare)
nginx_run_case = run_case_text(nginx_compare)
static_run_case = run_case_text(static_compare)

for name, (full_text, run_text) in {
    "redis sched_ext compare": (redis_compare, redis_run_case),
    "nginx sched_ext compare": (nginx_compare, nginx_run_case),
}.items():
    require("RUN_RANDOM_SEED" in full_text, f"{name}: missing random seed")
    require("write_run_orders" in full_text, f"{name}: missing planned run order writer")
    require("randomized_complete_block" in full_text, f"{name}: missing randomized block manifest")
    require("BENCH_CGROUP_ROOT" in full_text, f"{name}: missing dedicated benchmark cgroup root")
    default_block = case_block(run_text, "noisy_default")
    require("setup_cgroup_v2.sh" not in default_block, f"{name}: noisy_default must not setup EulerPilot cgroup")
    require("CASE_BACKGROUND_CGROUP" in default_block, f"{name}: noisy_default must use bench background cgroup")
    require("$BACKGROUND_CGROUP" not in default_block and "${BACKGROUND_CGROUP" not in default_block,
            f"{name}: noisy_default must not use EulerPilot background cgroup")
    require("--active" not in default_block, f"{name}: noisy_default must not run active Agent")
    cgroup_block = case_block(run_text, "noisy_cgroup_v2")
    require("setup_cgroup_v2.sh" in cgroup_block, f"{name}: cgroup treatment should still initialize cgroup v2")

default_static = case_block(static_run_case, "default_noisy")
require("setup_cgroup_v2.sh" not in default_static, "static-vs-agent: default_noisy must not setup EulerPilot cgroup")
require("$BACKGROUND_CGROUP" not in default_static and "${BACKGROUND_CGROUP" not in default_static,
        "static-vs-agent: default_noisy must not use EulerPilot background cgroup")
require("CASE_BACKGROUND_CGROUP" in default_static, "static-vs-agent: default_noisy must use bench background cgroup")
require("baseline_control=none" in default_static, "static-vs-agent: default_noisy must record no-control baseline")

observe_static = case_block(static_run_case, "agent_observe_only")
require("setup_cgroup_v2.sh" not in observe_static, "static-vs-agent: observe-only must not setup controlled cgroup")
require("CASE_BACKGROUND_CGROUP" in observe_static, "static-vs-agent: observe-only must use bench background cgroup")

manual_static = case_block(static_run_case, "manual_static")
require("setup_cgroup_v2.sh" in manual_static, "static-vs-agent: manual_static should initialize treatment cgroup")
require("BACKGROUND_CGROUP" in manual_static, "static-vs-agent: manual_static should use EulerPilot treatment cgroup")
require("cpu.max" in manual_static, "static-vs-agent: manual_static should write cpu.max")

print("benchmark_release_semantics=pass")
PY
