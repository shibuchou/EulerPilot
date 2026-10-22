#!/usr/bin/env python3
"""Unprivileged, read-only capability hints; not an attach/permission test."""
import argparse
import gzip
import json
from pathlib import Path
import platform
import re
import shutil
import sys


def read_optional(path):
    try:
        return Path(path).read_text(encoding="utf-8", errors="replace").strip()
    except OSError:
        return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--require-scx", action="store_true", help="exit 2 when basic sched_ext prerequisites are not observed")
    args = parser.parse_args()
    release = platform.release()
    linux = platform.system() == "Linux"
    btf = Path("/sys/kernel/btf/vmlinux").is_file() if linux else False
    scx_state = read_optional("/sys/kernel/sched_ext/state") if linux else None
    config = read_optional("/boot/config-" + release) if linux else None
    config_source = "/boot/config-" + release if config is not None else None
    if linux and config is None:
        try:
            with gzip.open("/proc/config.gz", "rt", encoding="utf-8", errors="replace") as stream:
                config = stream.read()
                config_source = "/proc/config.gz"
        except OSError:
            pass
    ext_config = None
    if config is not None:
        match = re.search(r"^CONFIG_SCHED_CLASS_EXT=(\S+)$", config, re.MULTILINE)
        ext_config = match.group(1) if match else "not-enabled-or-absent"
    mountinfo = read_optional("/proc/self/mountinfo") if linux else None
    cgroup2 = any(" - cgroup2 " in line for line in (mountinfo or "").splitlines())
    observed = linux and btf and scx_state is not None
    result = {
        "layer": "read-only-capability-hints",
        "system": platform.system(), "kernel": release,
        "os_release": read_optional("/etc/os-release") if linux else None,
        "btf_present": btf,
        "sched_ext_state": scx_state,
        "sched_ext_config": ext_config,
        "kernel_config_source": config_source,
        "cgroup2_mount_observed": cgroup2,
        "cgroup_controllers": read_optional("/sys/fs/cgroup/cgroup.controllers") if linux else None,
        "psi_cpu_present": Path("/proc/pressure/cpu").is_file() if linux else False,
        "pmu_cpu_device_present": Path("/sys/bus/event_source/devices/cpu").is_dir() if linux else False,
        "pmu_read_test": "NOT_RUN",
        "lsm_list": read_optional("/sys/kernel/security/lsm") if linux else None,
        "tools": {tool: shutil.which(tool) for tool in ("git", "clang", "gcc", "cmake", "bpftool", "perf", "python3")},
        "scx_prerequisite_hints": "OBSERVED" if observed else "NOT_OBSERVED",
        "scx_abi_and_attach": "NOT_TESTED",
        "delivery_environment_verified": False,
        "limitations": ["no load, attach, PMU read, cgroup delegation or policy write", "not a substitute for SP3 build/runtime evidence"],
    }
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 2 if args.require_scx and not observed else 0


if __name__ == "__main__":
    sys.exit(main())
