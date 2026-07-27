#!/usr/bin/env python3
"""Build an immutable EulerPilot formal artifact directory.

This script is intentionally conservative. It must be run from a clean
frozen worktree checked out at the candidate commit that already passed the
candidate-bound gates. Build outputs are written outside the source tree.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import socket
import stat
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


CORE_PATHS = [
    "agent",
    "bpf",
    "sched",
    "configs/agent.yaml",
    "configs/skills.yaml",
    "configs/policy.yaml",
    "configs/psi_gate.yaml",
    "configs/policy_engine_security_network_resource.yaml",
    "bench",
]

BUILD_SYSTEM_PATTERNS = [
    "Makefile",
    "CMakeLists.txt",
    "cmake",
    "scripts/build_formal_artifact.py",
    "scripts/build_scx_eulerpilot.sh",
    "scripts/collect_scx_stats.py",
    "package-lock.json",
    "web_console/package-lock.json",
]

ENV_WHITELIST = [
    "CC",
    "CXX",
    "CLANG",
    "BPFTOOL",
    "CFLAGS",
    "CXXFLAGS",
    "CPPFLAGS",
    "LDFLAGS",
    "PKG_CONFIG_PATH",
    "KERNEL_SRC",
    "SCHED_EXT_DIR",
    "LLVM",
    "MAKEFLAGS",
]


def project_root() -> Path:
    return Path(__file__).resolve().parents[1]


def run(root: Path, args: list[str], *, check: bool = True,
        env: dict[str, str] | None = None) -> str:
    proc = subprocess.run(
        args,
        cwd=root,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
        env=env,
    )
    if check and proc.returncode != 0:
        raise RuntimeError(
            f"command failed: {' '.join(args)}\n"
            f"stdout:\n{proc.stdout}\n"
            f"stderr:\n{proc.stderr}"
        )
    return proc.stdout.strip()


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


def resolve_commit(root: Path, rev: str) -> str:
    return run(root, ["git", "rev-parse", "--verify", f"{rev}^{{commit}}"])


def ensure_clean(root: Path) -> None:
    status = run(root, ["git", "status", "--porcelain"])
    if status:
        raise RuntimeError("source tree is dirty; formal artifact build refused")


def core_tree_hash(root: Path, commit: str) -> str:
    listing = run(root, ["git", "ls-tree", "-r", commit, "--", *CORE_PATHS])
    return sha256_bytes(listing.encode("utf-8"))


def file_tree_hash(root: Path, rels: list[str]) -> str:
    items: list[dict[str, str]] = []
    for rel in sorted(set(rels)):
        path = root / rel
        if not path.exists():
            continue
        if path.is_dir():
            for child in sorted(p for p in path.rglob("*") if p.is_file()):
                items.append({
                    "path": child.relative_to(root).as_posix(),
                    "sha256": sha256_file(child),
                })
        elif path.is_file():
            items.append({"path": rel, "sha256": sha256_file(path)})
    return sha256_json(items)


def command_version(root: Path, args: list[str]) -> str:
    try:
        return run(root, args, check=False).splitlines()[0]
    except Exception:
        return ""


def tool_versions(root: Path) -> dict[str, str]:
    versions = {
        "cc": command_version(root, [os.environ.get("CC", "gcc"), "--version"]),
        "cxx": command_version(root, [os.environ.get("CXX", "g++"), "--version"]),
        "clang": command_version(root, [os.environ.get("CLANG", "clang"), "--version"]),
        "bpftool": command_version(root, [os.environ.get("BPFTOOL", "bpftool"), "version"]),
        "make": command_version(root, ["make", "--version"]),
        "pkg_config_libbpf": command_version(root, ["pkg-config", "--modversion", "libbpf"]),
        "pkg_config_yaml_cpp": command_version(root, ["pkg-config", "--modversion", "yaml-cpp"]),
    }
    return versions


def environment_manifest() -> dict[str, str]:
    return {key: os.environ[key] for key in ENV_WHITELIST if key in os.environ}


def btf_hash() -> str:
    btf = Path("/sys/kernel/btf/vmlinux")
    return sha256_file(btf) if btf.exists() else ""


def host_build_manifest(root: Path) -> dict[str, Any]:
    payload = {
        "kind": "host-build",
        "host": socket.gethostname(),
        "kernel": command_version(root, ["uname", "-r"]),
        "os_release": "",
    }
    os_release = Path("/etc/os-release")
    if os_release.exists():
        payload["os_release"] = os_release.read_text(encoding="utf-8", errors="replace")
    payload["host_manifest_sha256"] = sha256_json(payload)
    return payload


def make_read_only(path: Path) -> None:
    for child in sorted(path.rglob("*"), reverse=True):
        if child.is_file():
            mode = child.stat().st_mode
            executable = bool(mode & (stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH))
            if executable:
                child.chmod(stat.S_IRUSR | stat.S_IXUSR |
                            stat.S_IRGRP | stat.S_IXGRP |
                            stat.S_IROTH | stat.S_IXOTH)
            else:
                child.chmod(stat.S_IRUSR | stat.S_IRGRP | stat.S_IROTH)
        elif child.is_dir():
            child.chmod(stat.S_IRUSR | stat.S_IXUSR |
                        stat.S_IRGRP | stat.S_IXGRP |
                        stat.S_IROTH | stat.S_IXOTH)
    path.chmod(stat.S_IRUSR | stat.S_IXUSR |
               stat.S_IRGRP | stat.S_IXGRP |
               stat.S_IROTH | stat.S_IXOTH)


def copy_if_exists(src: Path, dst: Path, artifact_dir: Path, manifest: dict[str, str]) -> None:
    if not src.exists():
        return
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)
    manifest[dst.relative_to(artifact_dir).as_posix()] = sha256_file(dst)


def find_scx_bpf_object(scx_build_dir: Path) -> Path | None:
    matches = sorted(scx_build_dir.rglob("scx_eulerpilot.bpf.o"))
    return matches[0] if matches else None


def build_scx(root: Path, build_dir: Path, env: dict[str, str]) -> tuple[Path, Path]:
    kernel_src = env.get("KERNEL_SRC", "").strip()
    if not kernel_src:
        raise RuntimeError(
            "KERNEL_SRC is required for formal SCX artifacts; "
            "use --skip-scx only for portable non-SCX artifact checks"
        )
    sched_ext_dir = Path(env.get("SCHED_EXT_DIR", str(Path(kernel_src) / "tools/sched_ext")))
    if not (sched_ext_dir / "Makefile").exists():
        raise RuntimeError(f"sched_ext Makefile not found: {sched_ext_dir / 'Makefile'}")

    scx_build_dir = build_dir / "scx-tools"
    scx_build_dir.mkdir(parents=True, exist_ok=True)
    shutil.copy2(root / "sched/scx_eulerpilot.c", sched_ext_dir / "scx_eulerpilot.c")
    shutil.copy2(root / "sched/scx_eulerpilot.bpf.c", sched_ext_dir / "scx_eulerpilot.bpf.c")
    makefile = sched_ext_dir / "Makefile"
    text = makefile.read_text(encoding="utf-8", errors="replace")
    if "scx_eulerpilot" not in text:
        makefile.write_text(
            text.replace("c-sched-targets = ", "c-sched-targets = scx_eulerpilot "),
            encoding="utf-8",
        )
    run(root, [
        "make",
        "-C",
        str(sched_ext_dir),
        f"O={scx_build_dir}",
        f"LLVM={env.get('LLVM', '1')}",
        "scx_eulerpilot",
        f"-j{os.cpu_count() or 1}",
    ], env=env)
    scx_bin = scx_build_dir / "build/bin/scx_eulerpilot"
    scx_bpf = find_scx_bpf_object(scx_build_dir)
    if not scx_bin.exists():
        raise RuntimeError(f"scx_eulerpilot binary not produced: {scx_bin}")
    if scx_bpf is None:
        raise RuntimeError(f"scx_eulerpilot.bpf.o not produced under: {scx_build_dir}")
    return scx_bin, scx_bpf


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tested-code-commit", required=True)
    parser.add_argument("--build-root", default="/root/eulerpilot-build")
    parser.add_argument("--artifact-root", default="/root/eulerpilot-artifacts")
    parser.add_argument("--build-attempt-id", default="")
    parser.add_argument("--skip-scx", action="store_true",
                        help="Build portable artifacts only; formal SCX gate will fail without SCX.")
    ns = parser.parse_args()

    root = project_root()
    ensure_clean(root)
    tested = resolve_commit(root, ns.tested_code_commit)
    head = resolve_commit(root, "HEAD")
    if head != tested:
        raise RuntimeError("HEAD must equal tested_code_commit in the frozen worktree")

    attempt = ns.build_attempt_id or datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    build_root = Path(ns.build_root).expanduser().resolve() / tested / attempt
    if build_root.exists():
        raise RuntimeError(f"build attempt already exists: {build_root}")
    build_root.mkdir(parents=True)

    build_dir = build_root / "make"
    env = os.environ.copy()
    env_manifest = environment_manifest()
    host_manifest = host_build_manifest(root)
    container_digest = os.environ.get(
        "EULERPILOT_CI_IMAGE_DIGEST",
        f"host-build:{host_manifest['host_manifest_sha256']}",
    )

    inputs = {
        "tested_code_commit": tested,
        "core_tree_hash": core_tree_hash(root, tested),
        "build_script_sha256": sha256_file(Path(__file__).resolve()),
        "build_system_files_sha256": file_tree_hash(root, BUILD_SYSTEM_PATTERNS),
        "submodule_status_sha256": sha256_bytes(
            run(root, ["git", "submodule", "status", "--recursive"], check=False).encode("utf-8")
        ),
        "compiler_versions": tool_versions(root),
        "compiler_flags": {
            "CFLAGS": os.environ.get("CFLAGS", ""),
            "CXXFLAGS": os.environ.get("CXXFLAGS", ""),
            "CPPFLAGS": os.environ.get("CPPFLAGS", ""),
        },
        "linker_flags": {"LDFLAGS": os.environ.get("LDFLAGS", "")},
        "build_parameters": {
            "make_build_dir": str(build_dir),
            "targets": [
                "agent",
                "observer",
                "network-policy",
                "network-qos-tc",
                "network-xdp",
                "security-policy",
            ],
            "skip_scx": ns.skip_scx,
        },
        "dependency_versions": tool_versions(root),
        "kernel_headers_identity": {
            "uname_r": command_version(root, ["uname", "-r"]),
            "kernel_src": os.environ.get("KERNEL_SRC", ""),
            "sched_ext_dir": os.environ.get("SCHED_EXT_DIR", ""),
        },
        "btf_sha256": btf_hash(),
        "container_or_ci_image_digest": container_digest,
        "environment_whitelist_sha256": sha256_json(env_manifest),
    }
    artifact_id = sha256_json(inputs)
    artifact_dir = Path(ns.artifact_root).expanduser().resolve() / tested / artifact_id
    if artifact_dir.exists():
        raise RuntimeError(f"artifact directory already exists; refusing overwrite: {artifact_dir}")

    targets = [
        "agent",
        "observer",
        "network-policy",
        "network-qos-tc",
        "network-xdp",
        "security-policy",
    ]
    run(root, ["make", f"BUILD_DIR={build_dir}", *targets], env=env)

    file_hashes: dict[str, str] = {}
    artifact_dir.mkdir(parents=True)
    copy_if_exists(build_dir / "eulerpilot-agent", artifact_dir / "bin/eulerpilot-agent", artifact_dir, file_hashes)
    copy_if_exists(build_dir / "workload_observer_dump", artifact_dir / "bin/workload_observer_dump", artifact_dir, file_hashes)
    copy_if_exists(build_dir / "workload_observer.bpf.o", artifact_dir / "bpf/workload_observer.bpf.o", artifact_dir, file_hashes)
    copy_if_exists(build_dir / "network_policy.bpf.o", artifact_dir / "bpf/network_policy.bpf.o", artifact_dir, file_hashes)
    copy_if_exists(build_dir / "network_qos_tc.bpf.o", artifact_dir / "bpf/network_qos_tc.bpf.o", artifact_dir, file_hashes)
    copy_if_exists(build_dir / "network_xdp.bpf.o", artifact_dir / "bpf/network_xdp.bpf.o", artifact_dir, file_hashes)
    copy_if_exists(build_dir / "security_policy.bpf.o", artifact_dir / "bpf/security_policy.bpf.o", artifact_dir, file_hashes)

    scx_bin = None
    scx_bpf = None
    if not ns.skip_scx:
        scx_bin, scx_bpf = build_scx(root, build_root, env)
        if scx_bin:
            copy_if_exists(scx_bin, artifact_dir / "bin/scx_eulerpilot", artifact_dir, file_hashes)
        if scx_bpf:
            copy_if_exists(scx_bpf, artifact_dir / "bpf/scx_eulerpilot.bpf.o", artifact_dir, file_hashes)

    manifest = {
        "schema_version": 1,
        "artifact_purpose": "formal",
        "selected_for_formal_experiments": True,
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "tested_code_commit": tested,
        "build_attempt_id": attempt,
        "artifact_id": artifact_id,
        "artifact_dir": str(artifact_dir),
        "build_root": str(build_root),
        "artifact_id_inputs": inputs,
        "environment_whitelist": env_manifest,
        "host_build_manifest": host_manifest,
        "file_hashes": file_hashes,
        "scx_built": bool(scx_bin and scx_bpf),
    }
    manifest_dir = artifact_dir / "manifests"
    manifest_dir.mkdir(parents=True, exist_ok=True)
    manifest["build_manifest_payload_sha256"] = sha256_json(manifest)
    manifest_path = manifest_dir / "build_manifest.json"
    manifest_path.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    (manifest_dir / "build_manifest.json.sha256").write_text(
        f"{sha256_file(manifest_path)}  build_manifest.json\n",
        encoding="utf-8",
    )
    make_read_only(artifact_dir)
    print(manifest_path)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1)
