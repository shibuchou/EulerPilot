#!/usr/bin/env python3
"""Create an external EulerPilot release bundle.

The script is intentionally conservative:
it refuses dirty source trees and rejects core-code changes between the
tested code commit and the release candidate commit.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path


CORE_PATHS = [
    "Makefile",
    "agent",
    "bpf",
    "sched",
    "configs/agent.yaml",
    "configs/skills.yaml",
    "configs/policy.yaml",
    "configs/psi_gate.yaml",
    "configs/policy_engine_security_network_resource.yaml",
    "bench",
    "scripts/build_formal_artifact.py",
    "scripts/build_scx_eulerpilot.sh",
    "scripts/formal_artifact_gate.py",
]

OPTIONAL_EVIDENCE_FILES = [
    "configs/final_evidence_manifest.json",
    "evidence/evidence_status_overrides.json",
    "reports/final_evidence_compact.json",
    "reports/final_evidence_compact.md",
    "submission/submission_manifest.md",
]


def run(root: Path, args: list[str], *, check: bool = True) -> str:
    proc = subprocess.run(
        args,
        cwd=root,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if check and proc.returncode != 0:
        raise RuntimeError(f"command failed: {' '.join(args)}\n{proc.stderr.strip()}")
    return proc.stdout.strip()


def project_root() -> Path:
    return Path(__file__).resolve().parents[1]


def resolve_commit(root: Path, rev: str) -> str:
    return run(root, ["git", "rev-parse", "--verify", f"{rev}^{{commit}}"])


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def core_tree_hash(root: Path, commit: str) -> str:
    listing = run(root, ["git", "ls-tree", "-r", commit, "--", *CORE_PATHS])
    return hashlib.sha256(listing.encode("utf-8")).hexdigest()


def ensure_clean(root: Path) -> None:
    status = run(root, ["git", "status", "--porcelain"])
    if status:
        raise RuntimeError("source tree is dirty; refuse to create release bundle")


def core_diff(root: Path, tested: str, candidate: str) -> list[str]:
    output = run(
        root,
        ["git", "diff", "--name-only", f"{tested}..{candidate}", "--", *CORE_PATHS],
    )
    return [line for line in output.splitlines() if line.strip()]


def write_sha256sums(bundle_dir: Path) -> None:
    lines: list[str] = []
    for path in sorted(bundle_dir.rglob("*")):
        if not path.is_file() or path.name == "SHA256SUMS":
            continue
        rel = path.relative_to(bundle_dir).as_posix()
        lines.append(f"{sha256_file(path)}  {rel}")
    (bundle_dir / "SHA256SUMS").write_text("\n".join(lines) + "\n", encoding="utf-8")


def copy_optional_files(root: Path, bundle_dir: Path) -> list[str]:
    copied: list[str] = []
    evidence_dir = bundle_dir / "evidence"
    for rel in OPTIONAL_EVIDENCE_FILES:
        src = root / rel
        if not src.exists():
            continue
        dst = evidence_dir / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)
        copied.append(str(dst.relative_to(bundle_dir)))
    return copied


def load_artifact_manifest(path: str, tested: str) -> dict:
    if not path:
        return {}
    manifest_path = Path(path).expanduser().resolve()
    if not manifest_path.exists():
        raise RuntimeError(f"artifact manifest not found: {manifest_path}")
    payload = json.loads(manifest_path.read_text(encoding="utf-8"))
    if payload.get("tested_code_commit") != tested:
        raise RuntimeError("artifact manifest tested_code_commit differs from release tested commit")
    if payload.get("artifact_purpose") != "formal" or payload.get("selected_for_formal_experiments") is not True:
        raise RuntimeError("artifact manifest is not selected formal artifact")
    required = ["artifact_id", "build_attempt_id", "file_hashes", "artifact_id_inputs"]
    missing = [field for field in required if not payload.get(field)]
    if missing:
        raise RuntimeError(f"artifact manifest missing fields: {', '.join(missing)}")
    return {
        "artifact_id": payload.get("artifact_id"),
        "build_attempt_id": payload.get("build_attempt_id"),
        "artifact_dir": payload.get("artifact_dir"),
        "build_manifest_path": str(manifest_path),
        "build_manifest_sha256": sha256_file(manifest_path),
        "artifact_purpose": payload.get("artifact_purpose"),
        "selected_for_formal_experiments": payload.get("selected_for_formal_experiments"),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
    parser.add_argument("--tested-code-commit", required=True)
    parser.add_argument("--release-candidate-commit", default="HEAD")
    parser.add_argument("--release-commit", default="")
    parser.add_argument("--tag", default="")
    parser.add_argument("--output-root", default="/root/eulerpilot-release")
    parser.add_argument("--artifact-manifest", default="",
                        help="Path to formal artifact build_manifest.json.")
    parser.add_argument("--force", action="store_true")
    ns = parser.parse_args()

    root = project_root()
    ensure_clean(root)

    tested = resolve_commit(root, ns.tested_code_commit)
    candidate = resolve_commit(root, ns.release_candidate_commit)
    release = resolve_commit(root, ns.release_commit or candidate)

    changed_core = core_diff(root, tested, candidate)
    if changed_core:
        print("core code changed after tested_code_commit:", file=sys.stderr)
        for path in changed_core:
            print(f"  {path}", file=sys.stderr)
        return 2
    tested_core_hash = core_tree_hash(root, tested)
    release_core_hash = core_tree_hash(root, release)
    artifact = load_artifact_manifest(ns.artifact_manifest, tested)

    output_root = Path(ns.output_root).expanduser().resolve()
    bundle_dir = output_root / f"eulerpilot-{ns.version}"
    if bundle_dir.exists():
        if not ns.force:
            raise RuntimeError(f"bundle dir already exists: {bundle_dir}")
        shutil.rmtree(bundle_dir)
    bundle_dir.mkdir(parents=True)

    archive = bundle_dir / f"eulerpilot-{ns.version}-source.tar.gz"
    run(
        root,
        [
            "git",
            "archive",
            "--format=tar.gz",
            f"--prefix=eulerpilot-{ns.version}/",
            "-o",
            str(archive),
            release,
        ],
    )

    copied_evidence = copy_optional_files(root, bundle_dir)
    manifest = {
        "schema_version": 1,
        "project": "EulerPilot",
        "version": ns.version,
        "tag": ns.tag,
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "tested_code_commit": tested,
        "release_candidate_commit": candidate,
        "release_commit": release,
        "tag_commit": "",
        "tested_core_tree_hash": tested_core_hash,
        "release_core_tree_hash": release_core_hash,
        "core_tree_hash": tested_core_hash,
        "core_code_equivalent": tested_core_hash == release_core_hash,
        "formal_artifact": artifact,
        "allowed_post_test_paths": [
            "docs/",
            "evidence/",
            "reports/",
            "results/",
            "dashboard/",
            "release/",
            "submission/",
            "configs/final_evidence_manifest.json",
            "VERSION",
            "CHANGELOG.md",
        ],
        "source_tree_clean_at_start": True,
        "source_tree_clean_at_end": True,
        "output_root": str(bundle_dir),
        "generated_files_manifest": copied_evidence,
        "source_archive": archive.name,
    }
    (bundle_dir / "manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    write_sha256sums(bundle_dir)
    print(bundle_dir)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1)
