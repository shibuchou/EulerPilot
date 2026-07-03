#!/usr/bin/env python3
"""Build a compact, curated evidence report for EulerPilot defense review."""

from __future__ import annotations

import argparse
import csv
import json
import re
import subprocess
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


DEFAULT_MANIFEST = "configs/final_evidence_manifest.json"
DEFAULT_MD = "reports/final_evidence_compact.md"
DEFAULT_JSON = "reports/final_evidence_compact.json"


KEY_FIELDS = [
    "result",
    "reason",
    "kernel",
    "transaction_id",
    "policy_id",
    "source_rule",
    "target_ref",
    "target_type",
    "target_cgroup",
    "host_veth",
    "qos_rate",
    "network_qos_rate",
    "xdp_drop_count",
    "total_drop_count",
    "xdp_udp_tuple_drop_count",
    "io_max_pressure",
    "io_weight_pressure",
    "limited_time_s",
    "deep_hook_runtime_note",
    "lsm_cred_alloc_blank_attach",
    "lsm_cred_transfer_attach",
    "lsm_cred_prepare_hits",
    "lsm_task_fix_setuid_hits",
]


def repo_root() -> Path:
    try:
        out = subprocess.check_output(["git", "rev-parse", "--show-toplevel"], text=True).strip()
        return Path(out)
    except Exception:
        return Path.cwd()


def read_text(path: Path, limit: int | None = None) -> str:
    try:
        data = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return ""
    if limit is not None and len(data) > limit:
        return data[:limit]
    return data


def parse_key_value(text: str) -> dict[str, str]:
    data: dict[str, str] = {}
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        key = key.strip()
        if re.fullmatch(r"[A-Za-z0-9_.-]+", key):
            data[key] = value.strip()
    return data


def parse_quality_gate(path: Path) -> dict[str, Any]:
    text = read_text(path)
    ok_count = len(re.findall(r"^ok\s+\d+\s+-", text, flags=re.MULTILINE))
    not_ok_count = len(re.findall(r"^not ok\s+\d+\s+-", text, flags=re.MULTILINE))
    plan_match = re.search(r"^1\.\.(\d+)$", text, flags=re.MULTILINE)
    planned = int(plan_match.group(1)) if plan_match else None
    optional_ok = len(re.findall(r"^ok\s+-", text, flags=re.MULTILINE))
    complete = "quality gate complete" in text
    result = "pass" if complete and not_ok_count == 0 and (planned is None or ok_count >= planned) else "unknown"
    return {
        "result": result,
        "planned": planned,
        "ok_count": ok_count,
        "not_ok_count": not_ok_count,
        "optional_ok_count": optional_ok,
        "quality_gate_complete": complete,
    }


def parse_csv_headline(path: Path) -> dict[str, Any]:
    if not path.exists():
        return {}
    try:
        with path.open("r", encoding="utf-8", newline="") as f:
            rows = list(csv.DictReader(f))
    except (OSError, csv.Error):
        return {}
    if not rows:
        return {}
    first = rows[0]
    headline: dict[str, Any] = {"csv_rows": len(rows)}
    for key in [
        "test",
        "quiet_default_rps_avg",
        "quiet_scx_normal_rps_avg",
        "noisy_default_rps_avg",
        "noisy_cgroup_v2_rps_avg",
        "noisy_scx_psi_rps_avg",
    ]:
        if key in first and first[key] != "":
            headline[key] = first[key]
    return headline


def summarize_result_dir(path: Path, entry: dict[str, Any]) -> tuple[dict[str, Any], list[str]]:
    summary: dict[str, Any] = {}
    warnings: list[str] = []
    summary_name = entry.get("summary") or "summary.txt"
    summary_path = path / summary_name
    if summary_path.exists():
        text = read_text(summary_path, limit=100_000)
        if summary_name.endswith(".txt"):
            summary.update(parse_key_value(text))
        else:
            summary["summary_excerpt"] = compact_excerpt(text)
    else:
        warnings.append(f"missing summary file: {summary_name}")

    for extra in ["deep_hook_status.txt", "anomaly_event_summary.txt"]:
        extra_path = path / extra
        if extra_path.exists():
            summary.update({f"{extra}:{k}": v for k, v in parse_key_value(read_text(extra_path)).items()})

    csv_summary = parse_csv_headline(path / "compare_summary_avg.csv")
    if csv_summary:
        summary.update(csv_summary)

    return summary, warnings


def compact_excerpt(text: str) -> str:
    lines = []
    for raw in text.splitlines():
        line = raw.strip()
        if line:
            lines.append(line)
        if len(lines) >= 3:
            break
    return " / ".join(lines)[:220]


def file_size(path: Path) -> int:
    try:
        return path.stat().st_size
    except OSError:
        return 0


def inspect_entry(root: Path, entry: dict[str, Any]) -> dict[str, Any]:
    rel = Path(entry["path"])
    path = root / rel
    kind = entry.get("kind", "file")
    exists = path.exists()
    required = bool(entry.get("required", False))
    warnings: list[str] = []
    summary: dict[str, Any] = {}
    evidence_files: list[dict[str, Any]] = []

    if exists and kind == "quality_gate":
        summary = parse_quality_gate(path)
    elif exists and path.is_dir():
        summary, warnings = summarize_result_dir(path, entry)
    elif exists and path.is_file():
        if path.name.endswith(".txt") or path.name.endswith(".log"):
            kv = parse_key_value(read_text(path, limit=100_000))
            if kv:
                summary.update(kv)
            else:
                summary["excerpt"] = compact_excerpt(read_text(path, limit=10_000))
        summary["bytes"] = file_size(path)

    for item in entry.get("files", []):
        file_path = path / item if path.is_dir() else root / item
        evidence_files.append(
            {
                "path": str((rel / item) if path.is_dir() else Path(item)).replace("\\", "/"),
                "exists": file_path.exists(),
                "bytes": file_size(file_path),
            }
        )
        if not file_path.exists():
            warnings.append(f"missing evidence file: {item}")

    result = summary.get("result", "present" if exists else "missing")
    if not exists:
        status = "missing_required" if required else "missing_optional"
    elif result == "pass":
        status = "pass"
    elif kind == "quality_gate" and summary.get("result") == "pass":
        status = "pass"
    else:
        status = "present"

    return {
        "category": entry.get("category", "uncategorized"),
        "name": entry.get("name", rel.name),
        "host": entry.get("host", ""),
        "path": str(rel).replace("\\", "/"),
        "kind": kind,
        "required": required,
        "exists": exists,
        "status": status,
        "notes": entry.get("notes", ""),
        "summary": summary,
        "evidence_files": evidence_files,
        "warnings": warnings,
    }


def git_status_short(root: Path, ignored_paths: set[str]) -> list[str]:
    try:
        out = subprocess.check_output(["git", "status", "--short"], cwd=root, text=True)
        lines = []
        for line in out.splitlines():
            if not line.strip():
                continue
            path = line[3:].strip().replace("\\", "/") if len(line) > 3 else line.strip()
            if path in ignored_paths:
                continue
            if line.startswith("?? reports/final_repo_consistency_20260630-"):
                continue
            lines.append(line)
        return lines
    except Exception:
        return []


def markdown_table(rows: list[list[str]]) -> str:
    if not rows:
        return ""
    out = ["| " + " | ".join(rows[0]) + " |", "| " + " | ".join(["---"] * len(rows[0])) + " |"]
    for row in rows[1:]:
        out.append("| " + " | ".join(cell.replace("\n", " ") for cell in row) + " |")
    return "\n".join(out)


def short_summary(summary: dict[str, Any]) -> str:
    parts: list[str] = []
    if "result" in summary:
        parts.append(f"result={summary['result']}")
    if "reason" in summary:
        parts.append(f"reason={summary['reason']}")
    for key in KEY_FIELDS:
        if key in ("result", "reason"):
            continue
        if key in summary:
            parts.append(f"{key}={summary[key]}")
        if len(parts) >= 5:
            break
    if not parts and "summary_excerpt" in summary:
        parts.append(str(summary["summary_excerpt"]))
    if not parts and "excerpt" in summary:
        parts.append(str(summary["excerpt"]))
    return "<br>".join(parts) if parts else "-"


def build_markdown(manifest: dict[str, Any], report: dict[str, Any]) -> str:
    entries = report["entries"]
    missing_required = [e for e in entries if e["status"] == "missing_required"]
    warning_entries = [e for e in entries if e["warnings"]]
    lines = [
        "# EulerPilot 最终证据压缩报告",
        "",
        f"生成时间：`{report['generated_at']}`",
        f"清单：`{report['manifest_path']}`",
        "",
        "本报告由 `scripts/collect_final_evidence.py` 根据白名单清单生成，用于把分散的双机结果压缩成答辩入口。它不会递归扫描全部 `results/`，缺失项会显式列出。",
        "",
        "## 总览",
        "",
        markdown_table(
            [
                ["指标", "值"],
                ["清单条目", str(len(entries))],
                ["必需缺失", str(len(missing_required))],
                ["带警告条目", str(len(warning_entries))],
                ["Git 工作区额外状态", str(len(report["git_status_short"]))],
            ]
        ),
        "",
    ]
    if missing_required:
        lines.extend(["## 必需缺失", ""])
        lines.append(
            markdown_table(
                [["分类", "主机", "名称", "路径"]]
                + [[e["category"], e["host"], e["name"], f"`{e['path']}`"] for e in missing_required]
            )
        )
        lines.append("")

    categories = []
    for entry in entries:
        if entry["category"] not in categories:
            categories.append(entry["category"])
    for category in categories:
        group = [e for e in entries if e["category"] == category]
        lines.extend([f"## {category}", ""])
        rows = [["状态", "主机", "名称", "路径", "摘要"]]
        for e in group:
            rows.append([e["status"], e["host"], e["name"], f"`{e['path']}`", short_summary(e["summary"])])
        lines.append(markdown_table(rows))
        lines.append("")

    if warning_entries:
        lines.extend(["## 警告", ""])
        rows = [["分类", "主机", "名称", "警告"]]
        for e in warning_entries:
            rows.append([e["category"], e["host"], e["name"], "<br>".join(e["warnings"])])
        lines.append(markdown_table(rows))
        lines.append("")

    if report["git_status_short"]:
        lines.extend(["## 生成时 Git 工作区状态", ""])
        lines.extend([f"- `{line}`" for line in report["git_status_short"]])
        lines.append("")

    lines.extend(
        [
            "## 使用方式",
            "",
            "```bash",
            "python3 scripts/collect_final_evidence.py",
            "python3 scripts/collect_final_evidence.py --strict",
            "```",
            "",
            "`--strict` 会在必需证据缺失或清单条目存在警告时返回非零退出码，适合最终提交前检查。",
            "",
        ]
    )
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", default=DEFAULT_MANIFEST)
    parser.add_argument("--output-md", default=DEFAULT_MD)
    parser.add_argument("--output-json", default=DEFAULT_JSON)
    parser.add_argument("--strict", action="store_true", help="Fail on required missing entries or warnings.")
    args = parser.parse_args()

    root = repo_root()
    manifest_path = root / args.manifest
    manifest = json.loads(read_text(manifest_path))
    entries = [inspect_entry(root, item) for item in manifest.get("entries", [])]
    ignored_status_paths = {
        str(Path(args.output_md)).replace("\\", "/"),
        str(Path(args.output_json)).replace("\\", "/"),
    }
    report = {
        "generated_at": datetime.now(timezone.utc).astimezone().isoformat(timespec="seconds"),
        "manifest_path": args.manifest,
        "manifest_title": manifest.get("title", ""),
        "git_status_short": git_status_short(root, ignored_status_paths),
        "entries": entries,
    }

    output_json = root / args.output_json
    output_md = root / args.output_md
    output_json.parent.mkdir(parents=True, exist_ok=True)
    output_md.parent.mkdir(parents=True, exist_ok=True)
    output_json.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    output_md.write_text(build_markdown(manifest, report), encoding="utf-8")

    missing_required = [e for e in entries if e["status"] == "missing_required"]
    warnings = [e for e in entries if e["warnings"]]
    print(f"wrote {args.output_md}")
    print(f"wrote {args.output_json}")
    print(f"entries={len(entries)} missing_required={len(missing_required)} warnings={len(warnings)}")
    if missing_required or (args.strict and warnings):
        for e in missing_required:
            print(f"missing required: {e['path']}")
        if args.strict:
            for e in warnings:
                print(f"warning: {e['path']}: {'; '.join(e['warnings'])}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
