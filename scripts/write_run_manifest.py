#!/usr/bin/env python3
import hashlib
import json
import os
import platform
import subprocess
import sys
from pathlib import Path


def sha256_file(path: str) -> str:
    p = Path(path)
    if not p.exists():
        return ""
    h = hashlib.sha256()
    h.update(p.read_bytes())
    return h.hexdigest()


def cmd_output(cmd: list[str]) -> str:
    try:
        return subprocess.check_output(cmd, text=True, stderr=subprocess.DEVNULL).strip()
    except Exception:
        return ""


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: write_run_manifest.py <output.json> <benchmark>", file=sys.stderr)
        return 1

    output = Path(sys.argv[1])
    benchmark = sys.argv[2]

    manifest = {
        "run_id": output.parent.name,
        "timestamp": cmd_output(["date", "--iso-8601=seconds"]),
        "host": cmd_output(["hostname"]),
        "kernel_release": cmd_output(["uname", "-r"]),
        "kernel_config_hash": "",
        "git_commit": cmd_output(["git", "-C", "/root/EulerPilot", "rev-parse", "HEAD"]),
        "git_tag": cmd_output(["git", "-C", "/root/EulerPilot", "describe", "--tags", "--always"]),
        "backend": os.environ.get("BACKEND", ""),
        "gate_mode": os.environ.get("EULERPILOT_GATE_MODE", ""),
        "sched_ext_switch_mode": "full",
        "benchmark": benchmark,
        "benchmark_command": os.environ.get("BENCHMARK_COMMAND", ""),
        "stress_ng_command": os.environ.get("STRESS_COMMAND", ""),
        "agent_config_hash": sha256_file("/root/EulerPilot/configs/agent.yaml"),
        "psi_gate_config_hash": sha256_file("/root/EulerPilot/configs/psi_gate.yaml"),
        "scheduler_config_hash": sha256_file("/root/EulerPilot/sched/scx_eulerpilot.bpf.c"),
        "cpu_topology": cmd_output(["bash", "-lc", "lscpu | sed -n '1,20p'"]),
        "cpu_governor": cmd_output(["bash", "-lc", "cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || true"]),
        "numa_info": cmd_output(["bash", "-lc", "numactl --hardware 2>/dev/null || true"]),
        "raw_log_paths": [],
        "summary_path": "",
        "figure_paths": [],
    }

    output.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
