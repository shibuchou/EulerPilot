#!/usr/bin/env python3
"""Reproduce timeout aggregation/EOF behavior with no root or kernel writes."""
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
OUT = Path(__file__).resolve().parent
BUILD = ROOT / "build/review"


def main():
    subprocess.run(["g++", "-std=c++20", "-pthread", "-I" + str(ROOT / "include"),
                    "-I" + str(ROOT / "third_party"), str(OUT / "helper_probe.cpp"),
                    str(BUILD / "libep_core.a"), "-o", str(BUILD / "helper_review_probe")], check=True)
    observations = []
    with tempfile.TemporaryDirectory(prefix="helper-review-", dir=BUILD) as temporary:
        directory = Path(temporary)
        hang = directory / "hang.sh"
        # exec ensures that SIGKILL targets the actual sleeper, with no descendants.
        hang.write_text("#!/bin/sh\nexec sleep 30\n")
        hang.chmod(0o700)
        eof = directory / "eof.py"
        eof.write_text("#!/usr/bin/python3\nimport os,time\nos.close(1)\nos.close(2)\ntime.sleep(30)\n")
        eof.chmod(0o700)
        for case, helper, count in (("one_hanging_helper", hang, 1),
                                    ("three_hanging_helpers", hang, 3),
                                    ("EOF_then_hang", eof, 1)):
            proc = subprocess.run([str(BUILD / "helper_review_probe"), str(helper),
                                   "200", str(count), str(directory / "unused-pins")],
                                  capture_output=True, text=True, timeout=8, check=True)
            result = json.loads(proc.stdout)
            result["case"] = case
            observations.append(result)
    result = {"scope": "local diagnostic; no real targets or kernel writes",
              "source_sha256": hashlib.sha256((ROOT / "src/skills/sched_skill.cpp").read_bytes()).hexdigest(),
              "observations": observations,
              "claims": {"refresh_has_one_helper_timeout_as_global_budget": False,
                         "EOF_path_uses_exact_same_deadline": False},
              "limitations": "Timings are observations on WSL, not hard bounds or SP3 measurements."}
    (OUT / "helper-timing.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
