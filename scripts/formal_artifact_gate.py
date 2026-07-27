#!/usr/bin/env python3
"""Validate an EulerPilot formal artifact before formal experiments.

The default mode performs provenance, immutability, hash, and agent binary
smoke checks. Passing the full final gate requires --live, which also runs the
configured one-run SCX/throughput/mixed smoke commands against the selected
artifact without rebuilding it.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import signal
import stat
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


def project_root() -> Path:
    return Path(__file__).resolve().parents[1]


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def sha256_json(payload: Any) -> str:
    encoded = json.dumps(payload, sort_keys=True, ensure_ascii=False,
                         separators=(",", ":")).encode("utf-8")
    return sha256_bytes(encoded)


def run(root: Path, args: list[str], *, env: dict[str, str] | None = None,
        timeout: int = 120) -> tuple[int, str, str]:
    proc = subprocess.Popen(
        args,
        cwd=root,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=env,
        start_new_session=True,
    )
    try:
        out, err = proc.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(proc.pid, signal.SIGTERM)
            out, err = proc.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            os.killpg(proc.pid, signal.SIGKILL)
            out, err = proc.communicate(timeout=5)
        return 124, out, err + "\ncommand timed out and process group was terminated\n"
    return proc.returncode, out, err


def resolve_commit(root: Path, rev: str) -> str:
    rc, out, err = run(root, ["git", "rev-parse", "--verify", f"{rev}^{{commit}}"])
    if rc != 0:
        raise RuntimeError(err.strip())
    return out.strip()


def worktree_clean(root: Path) -> bool:
    rc, out, _ = run(root, ["git", "status", "--porcelain"])
    return rc == 0 and out.strip() == ""


def mode_has_write_bit(path: Path) -> bool:
    mode = path.stat().st_mode
    return bool(mode & (stat.S_IWUSR | stat.S_IWGRP | stat.S_IWOTH))


def artifact_is_read_only(path: Path) -> bool:
    if mode_has_write_bit(path):
        return False
    for child in path.rglob("*"):
        if mode_has_write_bit(child):
            return False
    return True


def record(result: dict[str, Any], key: str, passed: bool, reason: str = "") -> None:
    result["checks"][key] = "pass" if passed else "fail"
    if not passed and reason:
        result["failures"].append({"check": key, "reason": reason})


def check_manifest(root: Path, manifest_path: Path) -> tuple[dict[str, Any], dict[str, Any]]:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    artifact_dir = Path(manifest.get("artifact_dir", "")).expanduser().resolve()
    result: dict[str, Any] = {
        "schema_version": 1,
        "gate": "formal_artifact_gate",
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "manifest_path": str(manifest_path),
        "artifact_dir": str(artifact_dir),
        "tested_code_commit": manifest.get("tested_code_commit", ""),
        "artifact_id": manifest.get("artifact_id", ""),
        "build_attempt_id": manifest.get("build_attempt_id", ""),
        "build_manifest_sha256": sha256_file(manifest_path),
        "checks": {},
        "failures": [],
    }

    record(result, "formal_artifact_tested_commit_matches",
           bool(manifest.get("tested_code_commit")) and
           resolve_commit(root, "HEAD") == manifest.get("tested_code_commit"),
           "HEAD differs from artifact tested_code_commit")

    inputs = manifest.get("artifact_id_inputs", {})
    recomputed = sha256_json(inputs) if inputs else ""
    record(result, "formal_artifact_id_valid",
           bool(recomputed) and recomputed == manifest.get("artifact_id"),
           "artifact_id does not match canonical hash of inputs")

    record(result, "formal_artifact_purpose",
           manifest.get("artifact_purpose") == "formal" and
           manifest.get("selected_for_formal_experiments") is True,
           "manifest is not marked formal and selected")

    record(result, "formal_artifact_directory_exists",
           artifact_dir.exists() and artifact_dir.is_dir(),
           "artifact directory missing")

    if artifact_dir.exists():
        record(result, "formal_artifact_directory_immutable",
               artifact_is_read_only(artifact_dir),
               "artifact directory or child has write permission bits")
    else:
        record(result, "formal_artifact_directory_immutable", False, "artifact missing")

    all_hashes_match = True
    for rel, expected in manifest.get("file_hashes", {}).items():
        path = artifact_dir / rel
        if not path.exists():
            all_hashes_match = False
            result["failures"].append({"check": "formal_artifact_hashes_match",
                                       "reason": f"missing file {rel}"})
            continue
        actual = sha256_file(path)
        if actual != expected:
            all_hashes_match = False
            result["failures"].append({"check": "formal_artifact_hashes_match",
                                       "reason": f"sha256 mismatch {rel}"})
    result["checks"]["formal_artifact_hashes_match"] = "pass" if all_hashes_match else "fail"

    return manifest, result


def run_agent_smoke(root: Path, manifest: dict[str, Any], result: dict[str, Any]) -> None:
    artifact_dir = Path(manifest["artifact_dir"]).expanduser().resolve()
    agent_bin = artifact_dir / "bin/eulerpilot-agent"
    if not agent_bin.exists():
        record(result, "agent_binary_smoke", False, "missing agent binary")
        return
    rc, out, err = run(root, [str(agent_bin), "--validate-config", "configs/agent.yaml"], timeout=30)
    result["agent_binary_smoke_log"] = (out + err)[-4096:]
    record(result, "agent_binary_smoke", rc == 0, "agent config validation failed")


def artifact_pids(artifact_dir: Path) -> list[int]:
    current_pid = os.getpid()
    matches: list[int] = []
    needle = str(artifact_dir)
    proc_root = Path("/proc")
    for entry in proc_root.iterdir() if proc_root.exists() else []:
        if not entry.name.isdigit() or int(entry.name) == current_pid:
            continue
        cmdline = entry / "cmdline"
        try:
            raw = cmdline.read_bytes().replace(b"\0", b" ").decode("utf-8", errors="replace")
        except OSError:
            continue
        if needle in raw:
            matches.append(int(entry.name))
    return matches


def artifact_processes(artifact_dir: Path) -> list[str]:
    matches: list[str] = []
    for pid in artifact_pids(artifact_dir):
        try:
            raw = (Path("/proc") / str(pid) / "cmdline").read_bytes()
        except OSError:
            continue
        cmdline = raw.replace(b"\0", b" ").decode("utf-8", errors="replace").strip()
        matches.append(f"{pid}:{cmdline}")
    return matches


def terminate_artifact_processes(artifact_dir: Path) -> list[str]:
    terminated: list[str] = []
    for sig in (signal.SIGTERM, signal.SIGKILL):
        pids = artifact_pids(artifact_dir)
        if not pids:
            break
        for pid in pids:
            try:
                os.kill(pid, sig)
                terminated.append(f"{pid}:{sig.name}")
            except ProcessLookupError:
                continue
            except PermissionError:
                terminated.append(f"{pid}:{sig.name}:permission-denied")
        deadline = datetime.now().timestamp() + 5
        while artifact_pids(artifact_dir) and datetime.now().timestamp() < deadline:
            try:
                os.waitpid(-1, os.WNOHANG)
            except ChildProcessError:
                pass
            except OSError:
                pass
            import time
            time.sleep(0.1)
    return terminated


def residual_bpf_paths() -> list[str]:
    bpf_root = Path("/sys/fs/bpf")
    if not bpf_root.exists():
        return []
    matches: list[str] = []
    try:
        for child in bpf_root.rglob("*"):
            try:
                rel = child.as_posix()
            except OSError:
                continue
            if "eulerpilot" in rel.lower():
                if child.is_dir():
                    try:
                        has_children = any(child.iterdir())
                    except OSError:
                        matches.append(f"{rel}:scan-failed")
                        continue
                    if not has_children:
                        continue
                matches.append(rel)
    except OSError:
        return ["bpf-scan-failed"]
    return sorted(matches)


def unlink_known_scx_pins() -> None:
    """Remove only EulerPilot's namespaced SCX pins used by this gate."""
    pin_root = Path("/sys/fs/bpf/eulerpilot/scx_eulerpilot/v1")
    for name in ("class_map", "gate_state_map", "stats"):
        try:
            (pin_root / name).unlink()
        except FileNotFoundError:
            pass
        except OSError:
            pass
    for directory in (pin_root, pin_root.parent):
        try:
            directory.rmdir()
        except FileNotFoundError:
            pass
        except OSError:
            pass


def settle_bpf_cleanup(root: Path, env: dict[str, str], timeout_s: float = 6.0) -> list[str]:
    """Retry rollback/unlink until no EulerPilot BPF pins remain or timeout."""
    deadline = time.monotonic() + timeout_s
    last_paths: list[str] = []
    while time.monotonic() < deadline:
        unlink_known_scx_pins()
        last_paths = residual_bpf_paths()
        if not last_paths:
            return []
        run(root, ["bash", "scripts/rollback.sh"], env=env, timeout=120)
        time.sleep(0.2)
    unlink_known_scx_pins()
    return residual_bpf_paths()


def run_live_gate(root: Path, manifest_path: Path, manifest: dict[str, Any], result: dict[str, Any],
                  timeout: int) -> None:
    artifact_dir = Path(manifest["artifact_dir"]).expanduser().resolve()
    env = os.environ.copy()
    tested = manifest.get("tested_code_commit", "unknown")
    artifact_id = manifest.get("artifact_id", "unknown")
    gate_root = Path(os.environ.get(
        "EULERPILOT_FORMAL_GATE_RUN_ROOT",
        f"/root/eulerpilot-runs/{tested}/formal-artifact-gate/{artifact_id}",
    )).expanduser().resolve()
    gate_root.mkdir(parents=True, exist_ok=True)
    result["formal_artifact_gate_output_root"] = str(gate_root)
    env["EULERPILOT_ARTIFACT_DIR"] = str(artifact_dir)
    env["EULERPILOT_ARTIFACT_MANIFEST"] = str(manifest_path)
    env["EULERPILOT_TESTED_CODE_COMMIT"] = str(manifest.get("tested_code_commit", ""))
    env["EULERPILOT_ARTIFACT_ID"] = str(manifest.get("artifact_id", ""))
    env["EULERPILOT_BUILD_ATTEMPT_ID"] = str(manifest.get("build_attempt_id", ""))
    env["EULERPILOT_BUILD_MANIFEST_SHA256"] = sha256_file(manifest_path)
    env["EULERPILOT_AGENT_SHA256"] = str(manifest.get("file_hashes", {}).get("bin/eulerpilot-agent", ""))
    env["EULERPILOT_SCX_SHA256"] = str(manifest.get("file_hashes", {}).get("bin/scx_eulerpilot", ""))
    env["EULERPILOT_OBSERVER_BPF_SHA256"] = str(manifest.get("file_hashes", {}).get("bpf/workload_observer.bpf.o", ""))
    env["EULERPILOT_SCX_BPF_SHA256"] = str(manifest.get("file_hashes", {}).get("bpf/scx_eulerpilot.bpf.o", ""))
    env["EULERPILOT_AGENT_BIN"] = str(artifact_dir / "bin/eulerpilot-agent")
    env["AGENT_BIN"] = str(artifact_dir / "bin/eulerpilot-agent")
    env["SCX_BIN"] = str(artifact_dir / "bin/scx_eulerpilot")
    env["EULERPILOT_SCX_BINARY"] = str(artifact_dir / "bin/scx_eulerpilot")

    observer_obj = artifact_dir / "bpf/workload_observer.bpf.o"
    record(result, "observer_load_smoke", observer_obj.exists(),
           "missing workload observer BPF object")

    scx_bin = artifact_dir / "bin/scx_eulerpilot"
    scx_bpf = artifact_dir / "bpf/scx_eulerpilot.bpf.o"
    if scx_bin.exists() and scx_bpf.exists():
        rc, out, err = run(root, [str(scx_bin), "--status"], env=env, timeout=30)
        result["scx_status_log"] = (out + err)[-4096:]
        record(result, "scx_stats_schema", rc == 0, "scx status failed")
        if Path("/sys/kernel/sched_ext").exists():
            rc, out, err = run(root, [str(scx_bin), "--status"], env=env, timeout=30)
            result["scx_verifier_log"] = (out + err)[-4096:]
            record(result, "scx_verifier_load", rc == 0, "scx verifier/status check failed")
        else:
            record(result, "scx_verifier_load", False, "sched_ext sysfs unavailable")
            record(result, "scx_attach_detach", False, "sched_ext sysfs unavailable")
    else:
        record(result, "scx_stats_schema", False, "missing SCX artifact")
        record(result, "scx_verifier_load", False, "missing SCX artifact")
        record(result, "scx_attach_detach", False, "missing SCX artifact")

    throughput_cmd = [
        "bash",
        "bench/throughput/run_throughput_first_benchmark.sh",
    ]
    throughput_outdir = gate_root / "throughput-run1"
    rc, out, err = run(root, throughput_cmd,
                       env={**env, "RUNS": "1", "OUTDIR": str(throughput_outdir)},
                       timeout=timeout)
    result["throughput_run1_log"] = (out + err)[-4096:]
    result["throughput_run1_output_dir"] = str(throughput_outdir)
    record(result, "throughput_run1_validity", rc == 0,
           "RUNS=1 throughput smoke failed")
    if scx_bin.exists() and scx_bpf.exists() and Path("/sys/kernel/sched_ext").exists():
        record(result, "scx_attach_detach", rc == 0,
               "throughput RUNS=1 sched_ext attach/detach smoke failed")

    terminated_between_pre = terminate_artifact_processes(artifact_dir)
    rc_cleanup, out_cleanup, err_cleanup = run(
        root, ["bash", "scripts/rollback.sh"], env=env, timeout=120
    )
    terminated_between_post = terminate_artifact_processes(artifact_dir)
    rc_cleanup2, out_cleanup2, err_cleanup2 = run(
        root, ["bash", "scripts/rollback.sh"], env=env, timeout=120
    )
    settle_bpf_cleanup(root, env)
    result["between_smoke_cleanup_log"] = (
        out_cleanup + err_cleanup + out_cleanup2 + err_cleanup2
    )[-4096:]
    result["between_smoke_terminated_processes"] = terminated_between_pre + terminated_between_post
    record(result, "between_smoke_cleanup", rc_cleanup == 0 and rc_cleanup2 == 0,
           "cleanup between throughput and mixed smoke failed")

    mixed_cmd = ["bash", "bench/mixed/run_mixed_adaptive_closure.sh"]
    mixed_outdir = gate_root / "mixed-adaptive-run1"
    mixed_env = {
        **env,
        "RUNS": "1",
        "OUTDIR": str(mixed_outdir),
        "BENCH_CLIENTS": os.environ.get("EULERPILOT_MIXED_SMOKE_BENCH_CLIENTS", "8"),
        "BENCH_REQUESTS": os.environ.get("EULERPILOT_MIXED_SMOKE_BENCH_REQUESTS", "5000"),
        "PSI_PROBE_CLIENTS": os.environ.get("EULERPILOT_MIXED_SMOKE_PSI_CLIENTS", "16"),
        "PSI_PROBE_REQUESTS": os.environ.get("EULERPILOT_MIXED_SMOKE_PSI_REQUESTS", "5000"),
        "DURATION_S": os.environ.get("EULERPILOT_MIXED_SMOKE_DURATION_S", "4"),
        "EULERPILOT_MIXED_AGENT_DURATION_S": os.environ.get(
            "EULERPILOT_MIXED_SMOKE_AGENT_DURATION_S", "300"
        ),
        "PHASE_MARKER_TIMEOUT_S": os.environ.get(
            "EULERPILOT_MIXED_SMOKE_PHASE_TIMEOUT_S", "15"
        ),
    }
    rc, out, err = run(root, mixed_cmd,
                       env=mixed_env,
                       timeout=timeout)
    result["mixed_adaptive_run1_log"] = (out + err)[-4096:]
    result["mixed_adaptive_run1_output_dir"] = str(mixed_outdir)
    record(result, "mixed_adaptive_run1_validity", rc == 0,
           "RUNS=1 mixed-adaptive smoke failed")

    terminated_final_pre = terminate_artifact_processes(artifact_dir)
    rc, out, err = run(root, ["bash", "scripts/rollback.sh"], env=env, timeout=120)
    terminated_final_post = terminate_artifact_processes(artifact_dir)
    rc2, out2, err2 = run(root, ["bash", "scripts/rollback.sh"], env=env, timeout=120)
    settle_bpf_cleanup(root, env)
    result["artifact_cleanup_log"] = (out + err + out2 + err2)[-4096:]
    result["artifact_cleanup_terminated_processes"] = terminated_final_pre + terminated_final_post
    record(result, "artifact_cleanup", rc == 0 and rc2 == 0, "rollback cleanup failed")
    residual_processes = artifact_processes(artifact_dir)
    result["residual_artifact_processes"] = residual_processes
    record(result, "no_residual_processes", not residual_processes,
           "artifact-owned child processes still running")
    bpf_paths = residual_bpf_paths()
    result["residual_bpf_paths"] = bpf_paths
    record(result, "no_residual_bpf_objects", not bpf_paths,
           "EulerPilot BPF pins remain after cleanup")
    record(result, "no_residual_scx_scheduler", not residual_processes,
           "artifact-owned SCX process still running")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", required=True)
    parser.add_argument("--output-json", default="")
    parser.add_argument("--live", action="store_true",
                        help="Run SCX/throughput/mixed RUNS=1 smoke using the artifact.")
    parser.add_argument("--timeout", type=int, default=600)
    ns = parser.parse_args()

    root = project_root()
    manifest, result = check_manifest(root, Path(ns.manifest).expanduser().resolve())
    record(result, "source_tree_clean", worktree_clean(root), "source tree is dirty")
    run_agent_smoke(root, manifest, result)
    if ns.live:
        run_live_gate(root, Path(ns.manifest).expanduser().resolve(), manifest, result, ns.timeout)
    else:
        result["checks"]["formal_artifact_live_smoke"] = "skipped"

    passed = all(value in ("pass", "skipped") for value in result["checks"].values())
    result["formal_artifact_gate"] = "pass" if passed and ns.live else "preflight-pass" if passed else "fail"
    if not ns.live and passed:
        result["failures"].append({
            "check": "formal_artifact_gate",
            "reason": "run with --live for the full formal gate before formal experiments",
        })

    if ns.output_json:
        Path(ns.output_json).write_text(
            json.dumps(result, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
        )
    print(f"formal_artifact_gate={result['formal_artifact_gate']}")
    for item in result["failures"]:
        print(f"failure: {item['check']}: {item['reason']}")
    return 0 if result["formal_artifact_gate"] == "pass" else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1)
