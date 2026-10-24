#!/usr/bin/env python3
"""Validate design contracts only; never apply policies or contact a host."""
import copy
import datetime as dt
import json
from pathlib import Path, PurePosixPath
import sys

try:
    import jsonschema
except ImportError:
    print("INCOMPLETE: jsonschema is unavailable; use an existing suitable environment.", file=sys.stderr)
    sys.exit(2)

ROOT = Path(__file__).resolve().parents[1]
SCHEMAS = ROOT / "design" / "schemas"
EXAMPLES = ROOT / "design" / "examples"


def load(path):
    with path.open(encoding="utf-8") as stream:
        return json.load(stream)


def check_policy_semantics(policy):
    spec = policy["spec"]
    params, safety = spec["parameters"], spec["safety"]
    errors = []
    if params["preempt_threshold_ns"] > params["lat_slice_ns"]:
        errors.append("preempt threshold exceeds latency slice")
    if safety["heartbeat_timeout_ms"] < 3 * safety["heartbeat_period_ms"]:
        errors.append("heartbeat timeout requires at least three periods")
    if safety["policy_ttl_ms"] <= safety["min_dwell_ms"] + safety["heartbeat_timeout_ms"]:
        errors.append("policy TTL cannot cover dwell plus heartbeat timeout")
    if (spec["expert"] == "safe-basic") != (spec["mode"] == "basic"):
        errors.append("safe-basic expert and basic mode must be selected together")
    if spec["expert"] == "cache-local" and not params["llc_affinity"]:
        errors.append("cache-local requires LLC affinity; runtime must also probe topology")
    paths = []
    for target in spec["targets"]:
        path = target["path"]
        parts = path.split("/")
        if ".." in parts or "." in parts or "//" in path or path.endswith("/"):
            errors.append("target path must be canonical")
        paths.append(PurePosixPath(path))
    for index, path in enumerate(paths):
        for other in paths[index + 1:]:
            if path == other or path in other.parents or other in path.parents:
                errors.append("overlapping target subtrees are not allowed in this contract")
    if not any(target["role"] == "FOREGROUND" for target in spec["targets"]):
        errors.append("this scheduling contract requires an explicit foreground target")
    if errors:
        raise ValueError("; ".join(errors))


def check_cgroup_semantics(policy):
    effects = policy["spec"]["effects"]
    paths = [effect["path"] for effect in effects]
    if len(set(paths)) != len(paths):
        raise ValueError("one cgroup path may appear only once per policy")


def check_skill_semantics(manifest):
    spec = manifest["spec"]
    caps = set(spec["capabilities"])
    if "apply" in caps and not {"snapshot", "verify", "rollback", "reconcile"}.issubset(caps):
        raise ValueError("apply requires snapshot, verify, rollback and reconcile")
    if "apply" in caps and spec["compensation"] != "supported-with-conflict-check":
        raise ValueError("this initial contract permits only compensatable apply skills")
    if not (SCHEMAS / spec["policy_schema"]).is_file():
        raise ValueError("referenced policy schema is missing")


def check_action_semantics(action):
    issued = dt.datetime.fromisoformat(action["issued_at"].replace("Z", "+00:00"))
    deadline = dt.datetime.fromisoformat(action["deadline"].replace("Z", "+00:00"))
    if issued.tzinfo is None or deadline.tzinfo is None:
        raise ValueError("timestamps require explicit timezone offsets")
    if not 0 < (deadline - issued).total_seconds() <= 60:
        raise ValueError("action deadline must be within 60 seconds of issue")
    # No live wall-clock/lease/hash check: this is deliberately an offline fixture.


def validate(schema, value, semantic):
    jsonschema.Draft7Validator(schema, format_checker=jsonschema.FormatChecker()).validate(value)
    semantic(value)


def main():
    contracts = [
        ("scheduling-policy.schema.json", "redis-policy.json", check_policy_semantics),
        ("cgroup-policy.schema.json", "cgroup-protect-scope.json", check_cgroup_semantics),
        ("skill-manifest.schema.json", "sched-skill.json", check_skill_semantics),
        ("action-envelope.schema.json", "apply-action.json", check_action_semantics),
        ("action-envelope.schema.json", "rollback-action.json", check_action_semantics),
    ]
    validators = {}
    for schema_name, example_name, semantic in contracts:
        schema, example = load(SCHEMAS / schema_name), load(EXAMPLES / example_name)
        jsonschema.Draft7Validator.check_schema(schema)
        validate(schema, example, semantic)
        validators[example_name] = (schema, example, semantic)
    cases = [
        ("redis-policy.json", ["spec", "parameters", "lat_slice_ns"], 0, "unbounded slice"),
        ("redis-policy.json", ["spec", "parameters", "preempt_threshold_ns"], 1500000, "inconsistent preempt"),
        ("redis-policy.json", ["spec", "safety", "policy_ttl_ms"], 10000, "expired before dwell"),
        ("redis-policy.json", ["spec", "safety", "heartbeat_period_ms"], 1000, "heartbeat timing"),
        ("redis-policy.json", ["spec", "safety", "allow_affinity_change"], True, "hidden resource change"),
        ("redis-policy.json", ["spec", "targets", 1, "role_source"], "inferred", "inferred BG role"),
        ("redis-policy.json", ["spec", "targets", 1, "path"], "/sys/fs/cgroup/eulerpilot-v2/redis/child", "overlapping subtree"),
        ("redis-policy.json", ["spec", "targets", 1, "path"], "/sys/fs/cgroup/../system.slice", "path traversal"),
        ("redis-policy.json", ["spec", "expert"], "safe-basic", "mode mismatch"),
        ("redis-policy.json", ["spec", "expert"], "cache-local", "missing topology feature"),
        ("sched-skill.json", ["spec", "capabilities"], ["apply"], "incomplete lifecycle"),
        ("sched-skill.json", ["spec", "execution"], "agent-direct-write", "second privileged writer"),
        ("apply-action.json", ["fence_epoch"], 0, "missing fencing"),
        ("apply-action.json", ["expected_generation"], -1, "invalid generation"),
        ("apply-action.json", ["deadline"], "2026-10-21T09:59:59+08:00", "invalid deadline"),
        ("apply-action.json", ["operation"], "policy.rollback", "rollback lacks original action"),
        ("rollback-action.json", ["operation"], "policy.apply", "apply contains rollback reference"),
        ("redis-policy.json", ["spec", "safety", "min_bg_work_units_per_sec"], 0, "no online background floor"),
        ("cgroup-protect-scope.json", ["spec", "effects", 0, "cpu_weight"], 0, "zero cpu weight"),
        ("cgroup-protect-scope.json", ["spec", "effects", 0, "path"], "/sys/fs/cgroup/../system.slice", "cgroup path traversal"),
        ("cgroup-protect-scope.json", ["spec", "source"], "rule-template", "autonomous cgroup writer"),
        ("cgroup-protect-scope.json", ["spec", "safety", "policy_ttl_ms"], 5000, "ttl below floor"),
    ]
    for example_name, keys, value, label in cases:
        schema, example, semantic = validators[example_name]
        bad = copy.deepcopy(example)
        target = bad
        for key in keys[:-1]:
            target = target[key]
        target[keys[-1]] = value
        try:
            validate(schema, bad, semantic)
        except (jsonschema.ValidationError, ValueError):
            continue
        raise AssertionError("negative fixture was accepted: " + label)
    print(json.dumps({
        "status": "PASS", "layer": "design-contract-only",
        "schemas": len({contract[0] for contract in contracts}), "positive_examples": len(contracts),
        "rejected_negative_examples": len(cases),
        "runtime_verified": False, "performance_verified": False,
        "limitations": ["no live target, ownership, deadline or digest validation", "no kernel attach or policy application"],
    }, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, AssertionError, jsonschema.ValidationError, jsonschema.SchemaError) as error:
        print("FAIL: " + str(error), file=sys.stderr)
        sys.exit(1)
