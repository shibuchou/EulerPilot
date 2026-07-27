#!/usr/bin/env python3
"""Verify an external EulerPilot release bundle."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run(repo: Path, args: list[str]) -> str:
    proc = subprocess.run(
        args,
        cwd=repo,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip())
    return proc.stdout.strip()


def verify_sums(bundle_dir: Path) -> None:
    sums = bundle_dir / "SHA256SUMS"
    if not sums.exists():
        raise RuntimeError("missing SHA256SUMS")
    for line in sums.read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        expected, rel = line.split(None, 1)
        path = bundle_dir / rel.strip()
        if not path.exists():
            raise RuntimeError(f"missing bundled file: {rel}")
        actual = sha256_file(path)
        if actual != expected:
            raise RuntimeError(f"sha256 mismatch: {rel}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bundle_dir")
    parser.add_argument("--repo", default="")
    ns = parser.parse_args()

    bundle_dir = Path(ns.bundle_dir).expanduser().resolve()
    manifest_path = bundle_dir / "manifest.json"
    if not manifest_path.exists():
        raise RuntimeError("missing manifest.json")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    for field in [
        "tested_code_commit",
        "release_candidate_commit",
        "release_commit",
        "core_tree_hash",
        "tested_core_tree_hash",
        "release_core_tree_hash",
        "source_archive",
    ]:
        if not manifest.get(field):
            raise RuntimeError(f"missing manifest field: {field}")
    if manifest.get("core_code_equivalent") is not True:
        raise RuntimeError("release manifest core_code_equivalent is not true")
    artifact = manifest.get("formal_artifact", {})
    for field in [
        "artifact_id",
        "build_attempt_id",
        "build_manifest_sha256",
        "artifact_purpose",
        "selected_for_formal_experiments",
    ]:
        if not artifact.get(field):
            raise RuntimeError(f"missing formal_artifact field: {field}")
    if artifact.get("artifact_purpose") != "formal" or artifact.get("selected_for_formal_experiments") is not True:
        raise RuntimeError("formal_artifact is not marked selected formal")
    if not (bundle_dir / manifest["source_archive"]).exists():
        raise RuntimeError("missing source archive")

    verify_sums(bundle_dir)

    if ns.repo:
        repo = Path(ns.repo).expanduser().resolve()
        release_commit = run(repo, ["git", "rev-parse", "--verify", f"{manifest['release_commit']}^{{commit}}"])
        if release_commit != manifest["release_commit"]:
            raise RuntimeError("release_commit does not resolve in repo")
        tag = manifest.get("tag", "")
        if tag:
            tag_commit = run(repo, ["git", "rev-list", "-n", "1", tag])
            if tag_commit != manifest["release_commit"]:
                raise RuntimeError("tag does not point to release_commit")

    print("release_bundle_verify=ok")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1)
