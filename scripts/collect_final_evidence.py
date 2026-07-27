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
DEFAULT_STATUS_OVERRIDES = "evidence/evidence_status_overrides.json"
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
    "static_vs_agent_validity",
    "static_vs_agent_groups",
    "throughput_first_validity",
    "mixed_adaptive_validity",
    "agent_overhead_validity",
    "evidence_override_status",
    "usable_for_final_positive_evidence",
]


STATIC_VS_AGENT_LABELS = [
    "default_noisy",
    "agent_observe_only",
    "manual_static",
    "agent_dynamic",
]

STATIC_VS_AGENT_RUN_FILES = [
    "summary.csv",
    "cpu_usage.env",
    "throttle.env",
    "controlled_pids.txt",
    "controlled_pid_cgroups.txt",
    "background_cgroup_procs.txt",
]

BACKGROUND_CGROUP_SUFFIX = "/eulerpilot/background"


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


def sha256_file(path: Path) -> str:
    import hashlib

    digest = hashlib.sha256()
    try:
        with path.open("rb") as handle:
            for chunk in iter(lambda: handle.read(1024 * 1024), b""):
                digest.update(chunk)
    except OSError:
        return ""
    return digest.hexdigest()


def sha256_path(path: Path) -> str:
    import hashlib

    if path.is_file():
        return sha256_file(path)
    if not path.is_dir():
        return ""
    rows = []
    for child in sorted(p for p in path.rglob("*") if p.is_file()):
        try:
            rel = child.relative_to(path).as_posix()
        except ValueError:
            rel = child.name
        rows.append(f"{sha256_file(child)}  {rel}")
    return hashlib.sha256(("\n".join(rows) + "\n").encode("utf-8")).hexdigest()


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


def load_status_overrides(root: Path, rel_path: str) -> dict[str, dict[str, Any]]:
    path = root / rel_path
    if not path.exists():
        return {}
    try:
        payload = json.loads(read_text(path))
    except json.JSONDecodeError:
        return {
            "__invalid_override_file__": {
                "new_status": "invalid",
                "invalid_reason": f"failed to parse {rel_path}",
                "usable_for_final_positive_evidence": False,
            }
        }
    overrides: dict[str, dict[str, Any]] = {}
    for item in payload.get("overrides", []):
        original = str(item.get("original_path", "")).strip().replace("\\", "/")
        if original:
            overrides[original] = item
    return overrides


def apply_status_override(entry: dict[str, Any],
                          override: dict[str, Any] | None,
                          actual_path: Path | None = None) -> None:
    if not override:
        return
    status = str(override.get("new_status", "")).strip()
    reason = str(override.get("invalid_reason", "")).strip()
    usable = bool(override.get("usable_for_final_positive_evidence", True))
    entry["override"] = override
    entry["summary"]["evidence_override_status"] = status
    entry["summary"]["usable_for_final_positive_evidence"] = str(usable).lower()
    if reason:
        entry["summary"]["evidence_override_reason"] = reason
    expected_sha = str(override.get("original_sha256", "")).strip()
    if expected_sha and actual_path is not None and actual_path.exists():
        actual_sha = sha256_path(actual_path)
        entry["summary"]["evidence_override_original_sha256"] = expected_sha
        entry["summary"]["evidence_override_actual_sha256"] = actual_sha
        if actual_sha != expected_sha:
            entry["warnings"].append(
                "evidence status override original_sha256 mismatch"
            )
    if status:
        entry["status"] = status
    if not usable:
        warning = f"evidence status override blocks final positive use: {status}"
        if reason:
            warning += f" ({reason})"
        entry["warnings"].append(warning)


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


def csv_fieldnames(path: Path) -> list[str]:
    if not path.exists():
        return []
    try:
        with path.open("r", encoding="utf-8", newline="") as f:
            reader = csv.DictReader(f)
            return list(reader.fieldnames or [])
    except (OSError, csv.Error):
        return []


def csv_rows(path: Path) -> list[dict[str, str]]:
    if not path.exists():
        return []
    try:
        with path.open("r", encoding="utf-8", newline="") as f:
            return list(csv.DictReader(f))
    except (OSError, csv.Error):
        return []


def parse_int(value: str | None) -> int:
    if value is None or value == "":
        return 0
    try:
        return int(float(value))
    except ValueError:
        return 0


def parse_float(value: str | None) -> float:
    if value is None or value == "":
        return 0.0
    try:
        return float(value)
    except ValueError:
        return 0.0


def cgroup_path_from_snapshot_line(line: str) -> str:
    parts = line.strip().split(maxsplit=1)
    if len(parts) != 2:
        return ""
    if parts[1] == "missing-proc-cgroup":
        return "missing-proc-cgroup"
    fields = parts[1].split(":")
    return fields[-1] if fields else ""


def validate_static_vs_agent_dir(path: Path) -> tuple[dict[str, Any], list[str]]:
    """Check that static-vs-Agent evidence controls the same stress workload."""
    summary: dict[str, Any] = {
        "static_vs_agent_groups": ",".join(STATIC_VS_AGENT_LABELS),
        "cpu_metric_scope": "system_proc_stat",
    }
    warnings: list[str] = []

    invalid_files = sorted(path.glob("run-*/*_invalid_reason.txt"))
    for invalid in invalid_files:
        rel = invalid.relative_to(path)
        reason = compact_excerpt(read_text(invalid, limit=10_000))
        warnings.append(f"invalid marker present: {rel}: {reason}")

    fields = csv_fieldnames(path / "compare_summary_avg.csv")
    if fields:
        for label in STATIC_VS_AGENT_LABELS:
            for suffix in ["rps_avg", "p99_ms_avg", "cpu_per_10k_requests_avg", "nr_throttled_delta_avg"]:
                key = f"{label}_{suffix}"
                if key not in fields:
                    warnings.append(f"compare_summary_avg.csv missing column: {key}")
    else:
        warnings.append("missing or unreadable compare_summary_avg.csv")

    run_dirs = sorted(p for p in path.glob("run-*") if p.is_dir())
    if not run_dirs:
        warnings.append("missing run-* directories")

    for run_dir in run_dirs:
        run_name = run_dir.name
        for label in STATIC_VS_AGENT_LABELS:
            for suffix in STATIC_VS_AGENT_RUN_FILES:
                file_path = run_dir / f"{label}_{suffix}"
                if not file_path.exists():
                    warnings.append(f"{run_name}/{label}_{suffix} missing")

            cpu_env = parse_key_value(read_text(run_dir / f"{label}_cpu_usage.env"))
            if cpu_env:
                if cpu_env.get("cpu_metric_scope") != "system_proc_stat":
                    warnings.append(f"{run_name}/{label}_cpu_usage.env missing cpu_metric_scope=system_proc_stat")
                if cpu_env.get("cpu_metric_warning") != "auxiliary_only_not_target_cgroup":
                    warnings.append(
                        f"{run_name}/{label}_cpu_usage.env missing cpu_metric_warning=auxiliary_only_not_target_cgroup"
                    )

            cgroup_file = run_dir / f"{label}_controlled_pid_cgroups.txt"
            if cgroup_file.exists():
                cgroup_lines = [line for line in read_text(cgroup_file).splitlines() if line.strip()]
                if not cgroup_lines:
                    warnings.append(f"{run_name}/{label}_controlled_pid_cgroups.txt empty")
                for line in cgroup_lines:
                    cgroup_path = cgroup_path_from_snapshot_line(line)
                    if cgroup_path == "missing-proc-cgroup":
                        warnings.append(f"{run_name}/{label} has missing /proc/<pid>/cgroup snapshot")
                    elif cgroup_path != BACKGROUND_CGROUP_SUFFIX:
                        warnings.append(f"{run_name}/{label} controlled pid outside background cgroup: {cgroup_path}")

            if label == "manual_static":
                throttle = parse_key_value(read_text(run_dir / f"{label}_throttle.env"))
                if parse_int(throttle.get("nr_throttled_delta")) <= 0:
                    warnings.append(f"{run_name}/manual_static did not record nr_throttled_delta > 0")

            if label in {"agent_observe_only", "agent_dynamic"}:
                snapshot = run_dir / f"{label}_agent_snapshot.txt"
                if not snapshot.exists() or file_size(snapshot) == 0:
                    warnings.append(f"{run_name}/{label}_agent_snapshot.txt missing or empty")

    summary["static_vs_agent_validity"] = "pass" if not warnings else "warning"
    summary["static_vs_agent_runs"] = len(run_dirs)
    return summary, warnings


def validate_throughput_first_dir(path: Path) -> tuple[dict[str, Any], list[str]]:
    summary: dict[str, Any] = {}
    warnings: list[str] = []
    invalid_files = sorted(path.glob("run-*/*_invalid_reason.txt"))
    for invalid in invalid_files:
        warnings.append(f"invalid marker present: {invalid.relative_to(path)}: {compact_excerpt(read_text(invalid))}")

    rows = {row.get("label", ""): row for row in csv_rows(path / "throughput_summary_avg.csv")}
    for label in ["default_batch", "cgroup_throughput_first", "scx_throughput_first"]:
        if label not in rows:
            warnings.append(f"throughput_summary_avg.csv missing label: {label}")

    cgroup = rows.get("cgroup_throughput_first", {})
    scx = rows.get("scx_throughput_first", {})
    if parse_float(cgroup.get("throughput_profile_hits_avg")) <= 0:
        warnings.append("cgroup_throughput_first did not record throughput_profile_hits_avg > 0")
    if parse_float(scx.get("throughput_profile_hits_avg")) <= 0:
        warnings.append("scx_throughput_first did not record throughput_profile_hits_avg > 0")
    if parse_float(scx.get("class_hits_batch_avg")) <= 0:
        warnings.append("scx_throughput_first did not record batch classification evidence")
    enqueue_batch_avg = parse_float(scx.get("enqueue_batch_avg"))
    batch_dispatch_total_avg = parse_float(scx.get("batch_dispatch_total_avg"))
    if enqueue_batch_avg > 0 and batch_dispatch_total_avg <= 0:
        warnings.append("scx_throughput_first enqueued batch work without class-aware batch dispatch")
    if "counter_delta_valid_avg" in scx and parse_float(scx.get("counter_delta_valid_avg")) < 1:
        warnings.append("scx_throughput_first counter_delta_valid_avg is not fully valid")
    if "dispatch_accounting_valid_avg" in scx and parse_float(scx.get("dispatch_accounting_valid_avg")) < 1:
        warnings.append("scx_throughput_first dispatch_accounting_valid_avg is not fully valid")
    if "workload_completion_valid_avg" in scx and parse_float(scx.get("workload_completion_valid_avg")) < 1:
        warnings.append("scx_throughput_first workload_completion_valid_avg is not fully valid")

    run_dirs = sorted(p for p in path.glob("run-*") if p.is_dir())
    if not run_dirs:
        warnings.append("missing run-* directories")
    for run_dir in run_dirs:
        for label in ["default_batch", "cgroup_throughput_first", "scx_throughput_first"]:
            for suffix in ["summary.csv", "worker_pids.txt", "metrics.env"]:
                if not (run_dir / f"{label}_{suffix}").exists():
                    warnings.append(f"{run_dir.name}/{label}_{suffix} missing")
        for label in ["cgroup_throughput_first", "scx_throughput_first"]:
            snapshot = run_dir / f"{label}_agent_snapshot.txt"
            if not snapshot.exists() or file_size(snapshot) == 0:
                warnings.append(f"{run_dir.name}/{label}_agent_snapshot.txt missing or empty")
        scx_metrics = parse_key_value(read_text(run_dir / "scx_throughput_first_metrics.env"))
        if scx_metrics:
            if parse_int(scx_metrics.get("counter_delta_valid", "1")) != 1:
                warnings.append(f"{run_dir.name}/scx_throughput_first counter delta invalid")
            enqueue_batch = parse_int(scx_metrics.get("enqueue_batch"))
            batch_dispatch_total = parse_int(scx_metrics.get("batch_dispatch_total"))
            if enqueue_batch > 0 and batch_dispatch_total <= 0:
                warnings.append(f"{run_dir.name}/scx_throughput_first enqueue_batch without class-aware dispatch")

    summary["throughput_first_validity"] = "pass" if not warnings else "invalid"
    summary["throughput_first_runs"] = len(run_dirs)
    return summary, warnings


def validate_mixed_adaptive_dir(path: Path) -> tuple[dict[str, Any], list[str]]:
    summary: dict[str, Any] = {}
    warnings: list[str] = []
    invalid_files = sorted(path.glob("run-*/invalid_reason.txt"))
    for invalid in invalid_files:
        warnings.append(f"invalid marker present: {invalid.relative_to(path)}: {compact_excerpt(read_text(invalid))}")

    rows = {row.get("phase", ""): row for row in csv_rows(path / "mixed_adaptive_summary.csv")}
    for phase in ["quiet_pre", "pressure_active", "recovery"]:
        if phase not in rows:
            warnings.append(f"mixed_adaptive_summary.csv missing phase: {phase}")

    pressure = rows.get("pressure_active", {})
    recovery = rows.get("recovery", {})
    if parse_int(pressure.get("active_seen_count")) <= 0:
        warnings.append("pressure_active did not record ACTIVE transition")
    if parse_int(pressure.get("scheduler_update_evidence_count")) <= 0:
        warnings.append("pressure_active did not record scheduler update evidence")
    if parse_float(pressure.get("switch_latency_ms_avg")) <= 0:
        warnings.append("pressure_active missing switch latency")
    if parse_int(recovery.get("recovery_seen_count")) <= 0:
        warnings.append("recovery did not record recovery evidence")
    # Recovery starts while the gate is still ACTIVE and must then converge through
    # COOLDOWN back to NORMAL. The per-run trace-chain validator below enforces
    # that ordered sequence using one agent_instance_id and monotonic event_seq.

    run_dirs = sorted(p for p in path.glob("run-*") if p.is_dir())
    if not run_dirs:
        warnings.append("missing run-* directories")
    for run_dir in run_dirs:
        combined_trace = run_dir / "combined_psi_gate_trace.jsonl"
        if not combined_trace.exists():
            warnings.append(f"{run_dir.name}/combined_psi_gate_trace.jsonl missing")
        else:
            chain_valid, chain_reason = validate_mixed_trace_chain(combined_trace)
            if not chain_valid:
                warnings.append(f"{run_dir.name}/combined_psi_gate_trace.jsonl invalid: {chain_reason}")
        for phase in ["quiet_pre", "pressure_active", "recovery"]:
            for suffix in ["summary.csv", "agent_snapshot.txt", "gate_status.txt", "psi_gate_trace.jsonl"]:
                file_path = run_dir / f"{phase}_{suffix}"
                if not file_path.exists() or file_size(file_path) == 0:
                    warnings.append(f"{run_dir.name}/{phase}_{suffix} missing or empty")
        if not (run_dir / "rollback_after.log").exists():
            warnings.append(f"{run_dir.name}/rollback_after.log missing")

    summary["mixed_adaptive_validity"] = "pass" if not warnings else "invalid"
    summary["mixed_adaptive_runs"] = len(run_dirs)
    return summary, warnings


def validate_mixed_trace_chain(path: Path) -> tuple[bool, str]:
    events = []
    for raw in read_text(path, limit=None).splitlines():
        try:
            events.append(json.loads(raw))
        except json.JSONDecodeError:
            continue
    if not events:
        return False, "empty-or-unparseable-trace"
    instances = {event.get("agent_instance_id") for event in events if event.get("agent_instance_id")}
    if len(instances) != 1:
        return False, "agent-instance-id-not-unique"
    seqs = [parse_int(str(event.get("event_seq"))) for event in events if "event_seq" in event]
    if not seqs or seqs != sorted(seqs) or len(seqs) != len(set(seqs)):
        return False, "event-seq-not-strictly-increasing"
    timestamps = [parse_int(str(event.get("monotonic_timestamp_ns"))) for event in events
                  if "monotonic_timestamp_ns" in event]
    if timestamps and timestamps != sorted(timestamps):
        return False, "monotonic-timestamp-regressed"
    states = [event.get("next_state") or event.get("gate_state") for event in events
              if event.get("event_type") == "gate"]
    target = ["NORMAL", "ARMED", "ACTIVE", "COOLDOWN", "NORMAL"]
    index = 0
    for state in states:
        if index < len(target) and state == target[index]:
            index += 1
    if index != len(target):
        return False, "missing-ordered-normal-armed-active-cooldown-normal-chain"
    markers = [(i, event.get("phase")) for i, event in enumerate(events)
               if event.get("event_type") == "phase_marker"]
    marker_phases = [phase for _, phase in markers]
    if not {"quiet", "pressure", "recovery"}.issubset(set(marker_phases)):
        return False, "missing-agent-written-phase-marker"
    marker_index: dict[str, int] = {}
    for index_in_events, phase in markers:
        marker_index.setdefault(str(phase), index_in_events)
    if not (marker_index["quiet"] < marker_index["pressure"] < marker_index["recovery"]):
        return False, "phase-markers-out-of-order"

    def gate_states_between(start: int, end: int | None) -> list[str]:
        window = events[start + 1:end]
        return [str(event.get("next_state") or event.get("gate_state"))
                for event in window if event.get("event_type") == "gate"]

    def contains_ordered(values: list[str], expected: list[str]) -> bool:
        cursor = 0
        for value in values:
            if cursor < len(expected) and value == expected[cursor]:
                cursor += 1
        return cursor == len(expected)

    quiet_states = gate_states_between(marker_index["quiet"], marker_index["pressure"])
    pressure_states = gate_states_between(marker_index["pressure"], marker_index["recovery"])
    recovery_states = gate_states_between(marker_index["recovery"], None)
    if "NORMAL" not in quiet_states:
        return False, "quiet-phase-missing-stable-normal"
    if not contains_ordered(pressure_states, ["ARMED", "ACTIVE"]):
        return False, "pressure-phase-missing-armed-active"
    if not contains_ordered(recovery_states, ["COOLDOWN", "NORMAL"]):
        return False, "recovery-phase-missing-cooldown-normal"
    return True, "ok"


def validate_agent_overhead_dir(path: Path) -> tuple[dict[str, Any], list[str]]:
    summary: dict[str, Any] = {}
    warnings: list[str] = []
    rows = {row.get("label", ""): row for row in csv_rows(path / "agent_overhead_summary_avg.csv")}
    for label in ["observe_only_cgroup", "active_cgroup", "active_sched_ext"]:
        row = rows.get(label)
        if not row:
            warnings.append(f"agent_overhead_summary_avg.csv missing label: {label}")
            continue
        if parse_int(row.get("runs_present")) <= 0:
            warnings.append(f"{label} has no present runs")
        if parse_int(row.get("skipped")) > 0:
            warnings.append(f"{label} unexpectedly skipped")
        if parse_float(row.get("cpu_percent_of_one_core_avg")) <= 0:
            warnings.append(f"{label} missing CPU overhead metric")
        if parse_float(row.get("rss_kb_avg")) <= 0:
            warnings.append(f"{label} missing RSS metric")

    run_dirs = sorted(p for p in path.glob("run-*") if p.is_dir())
    if not run_dirs:
        warnings.append("missing run-* directories")
    for run_dir in run_dirs:
        for label in ["observe_only_cgroup", "active_cgroup", "active_sched_ext"]:
            for suffix in ["samples.csv", "agent_snapshot.txt", "rollback_after.log"]:
                file_path = run_dir / f"{label}_{suffix}"
                if not file_path.exists() or file_size(file_path) == 0:
                    warnings.append(f"{run_dir.name}/{label}_{suffix} missing or empty")

    summary["agent_overhead_validity"] = "pass" if not warnings else "warning"
    summary["agent_overhead_runs"] = len(run_dirs)
    return summary, warnings


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
    elif not entry.get("allow_missing_summary", False):
        warnings.append(f"missing summary file: {summary_name}")

    for extra in ["deep_hook_status.txt", "anomaly_event_summary.txt"]:
        extra_path = path / extra
        if extra_path.exists():
            summary.update({f"{extra}:{k}": v for k, v in parse_key_value(read_text(extra_path)).items()})

    csv_summary = parse_csv_headline(path / "compare_summary_avg.csv")
    if csv_summary:
        summary.update(csv_summary)

    if path.name.startswith("redis-static-vs-agent-"):
        static_summary, static_warnings = validate_static_vs_agent_dir(path)
        summary.update(static_summary)
        warnings.extend(static_warnings)
    elif path.name.startswith("throughput-first-"):
        throughput_summary, throughput_warnings = validate_throughput_first_dir(path)
        summary.update(throughput_summary)
        warnings.extend(throughput_warnings)
    elif path.name.startswith("mixed-adaptive-"):
        mixed_summary, mixed_warnings = validate_mixed_adaptive_dir(path)
        summary.update(mixed_summary)
        warnings.extend(mixed_warnings)
    elif path.name.startswith("agent-overhead-"):
        overhead_summary, overhead_warnings = validate_agent_overhead_dir(path)
        summary.update(overhead_summary)
        warnings.extend(overhead_warnings)

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


def inspect_entry(root: Path,
                  entry: dict[str, Any],
                  status_overrides: dict[str, dict[str, Any]]) -> dict[str, Any]:
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

    inspected = {
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
    apply_status_override(inspected, status_overrides.get(inspected["path"]), path)
    return inspected


def validate_single_path(root: Path,
                         rel_or_abs: str,
                         status_overrides: dict[str, dict[str, Any]],
                         mode: str) -> int:
    raw = Path(rel_or_abs)
    path = raw if raw.is_absolute() else root / raw
    try:
        rel = path.relative_to(root)
    except ValueError:
        rel = raw
    entry = {
        "path": rel.as_posix(),
        "kind": "result_dir" if path.is_dir() else "file",
        "required": True,
        "summary": "summary.txt",
        "allow_missing_summary": True,
        "category": mode,
        "name": path.name,
    }
    inspected = inspect_entry(root, entry, status_overrides)
    output = {
        "mode": mode,
        "path": inspected["path"],
        "exists": inspected["exists"],
        "status": inspected["status"],
        "summary": inspected["summary"],
        "warnings": inspected["warnings"],
    }
    print(json.dumps(output, ensure_ascii=False, indent=2))
    return 1 if (not inspected["exists"] or inspected["warnings"]) else 0


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
        f"状态覆盖：`{report['status_overrides_path']}`，覆盖条目 `{report['status_overrides_count']}`",
        "",
        "本报告由 `scripts/collect_final_evidence.py` 根据白名单清单生成，用于把分散的双机结果压缩成答辩入口。它不会递归扫描全部 `results/`，缺失项会显式列出；若 `evidence/evidence_status_overrides.json` 将旧证据标为 provisional 或 invalid，strict 模式会阻止其作为最终正向证据。",
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
            "python3 scripts/collect_final_evidence.py --validate-run <result-dir>",
            "python3 scripts/collect_final_evidence.py --validate-suite <suite-dir>",
            "python3 scripts/collect_final_evidence.py --validate-release",
            "```",
            "",
            "`--validate-run` 用于单轮 smoke，`--validate-suite` 用于正式实验组，`--validate-release` 等价于最终 strict release gate。",
            "",
        ]
    )
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", default=DEFAULT_MANIFEST)
    parser.add_argument("--status-overrides", default=DEFAULT_STATUS_OVERRIDES)
    parser.add_argument("--output-md", default=DEFAULT_MD)
    parser.add_argument("--output-json", default=DEFAULT_JSON)
    parser.add_argument("--strict", action="store_true", help="Fail on required missing entries or warnings.")
    parser.add_argument("--validate-run", metavar="PATH", default="",
                        help="Validate one result directory or file for RUNS=1/development smoke.")
    parser.add_argument("--validate-suite", metavar="PATH", default="",
                        help="Validate one formal experiment suite directory.")
    parser.add_argument("--validate-release", action="store_true",
                        help="Run final release evidence validation; equivalent to --strict.")
    args = parser.parse_args()

    root = repo_root()
    status_overrides = load_status_overrides(root, args.status_overrides)
    if args.validate_run:
        return validate_single_path(root, args.validate_run, status_overrides, "validate-run")
    if args.validate_suite:
        return validate_single_path(root, args.validate_suite, status_overrides, "validate-suite")
    if args.validate_release:
        args.strict = True
    manifest_path = root / args.manifest
    manifest = json.loads(read_text(manifest_path))
    entries = [inspect_entry(root, item, status_overrides) for item in manifest.get("entries", [])]
    ignored_status_paths = {
        str(Path(args.output_md)).replace("\\", "/"),
        str(Path(args.output_json)).replace("\\", "/"),
    }
    report = {
        "generated_at": datetime.now(timezone.utc).astimezone().isoformat(timespec="seconds"),
        "manifest_path": args.manifest,
        "status_overrides_path": args.status_overrides,
        "status_overrides_count": len([k for k in status_overrides if not k.startswith("__")]),
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
