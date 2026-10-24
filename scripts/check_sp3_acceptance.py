#!/usr/bin/env python3
"""Strict evidence checks for accept_sp3.sh; never load BPF or remove resources."""
import argparse
import json
from pathlib import Path
import sys
import xml.etree.ElementTree as ET

REQUIRED_TESTS = {
    "unit", "integration", "integration_sched", "integration_fd",
    "integration_lease", "integration_ttl", "integration_cgroup",
    "integration_events",
}


def validate_ctest(root):
    cases = list(root.iter("testcase"))
    names = [case.get("name", "") for case in cases]
    if len(set(names)) != len(names):
        raise ValueError("duplicate CTest test names")
    missing = REQUIRED_TESTS - set(names)
    if missing:
        raise ValueError("required CTest tests missing: " + ", ".join(sorted(missing)))
    for case in cases:
        if any(case.find(tag) is not None for tag in ("skipped", "failure", "error")):
            raise ValueError("CTest test did not pass: " + case.get("name", ""))
        if case.get("status", "run") not in ("run", "passed"):
            raise ValueError("CTest test was not run: " + case.get("name", ""))
    return {"status": "PASS", "layer": "integration-tests-only",
            "tests": sorted(names), "passed": len(cases), "skipped": 0}


def validate_idle(snapshot):
    if snapshot["state"] != "disabled":
        raise ValueError("sched_ext is not disabled")
    if snapshot["pins"] != 0:
        raise ValueError("pin root is not empty; refusing to delete unknown resources")
    if snapshot["loader_pids"] or snapshot["executor_pids"]:
        raise ValueError("loader/executor processes remain: " + json.dumps(snapshot))
    return snapshot


def observe_idle():
    os_release = Path("/etc/os-release").read_text()
    fields = {}
    for line in os_release.splitlines():
        key, sep, value = line.partition("=")
        if sep:
            fields[key] = value.strip('"')
    if fields.get("ID", "").lower() != "openeuler" or "LTS-SP3" not in fields.get("PRETTY_NAME", ""):
        raise ValueError("this acceptance requires openEuler LTS-SP3")
    state = Path("/sys/kernel/sched_ext/state").read_text().strip()
    pin_root = Path("/sys/fs/bpf/eulerpilot-v2")
    if pin_root.is_symlink():
        raise ValueError("pin root must not be a symlink")
    pins = len(list(pin_root.iterdir())) if pin_root.exists() else 0
    processes = {"ep-sched-loader": [], "ep-executord": []}
    for entry in Path("/proc").iterdir():
        if not entry.name.isdecimal():
            continue
        try:
            name = (entry / "comm").read_text().strip()
        except FileNotFoundError:
            continue  # A process can exit between enumeration and read.
        if name in processes:
            processes[name].append(int(entry.name))
    return validate_idle({"state": state, "pins": pins,
                          "loader_pids": sorted(processes["ep-sched-loader"]),
                          "executor_pids": sorted(processes["ep-executord"])})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    ctest = sub.add_parser("ctest")
    ctest.add_argument("xml", type=Path)
    idle = sub.add_parser("idle")
    idle.add_argument("--phase", required=True, choices=("initial", "final"))
    args = parser.parse_args()
    if args.command == "ctest":
        result = validate_ctest(ET.parse(args.xml).getroot())
    else:
        result = observe_idle()
        result["phase"] = args.phase
        result["status"] = "PASS"
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, ET.ParseError) as error:
        print("[FAIL] " + str(error), file=sys.stderr)
        sys.exit(1)
