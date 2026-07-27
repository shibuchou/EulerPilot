#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  scripts/prepare_v6_candidate_worktree.sh [--dry-run|--apply]

Prepare a clean candidate worktree from the current v6 closeout working tree.

Defaults:
  --dry-run
  EULERPILOT_SOURCE_ROOT   current repository root
  EULERPILOT_CANDIDATE_ROOT /root/EulerPilot-candidate
  EULERPILOT_CANDIDATE_BRANCH closeout/v6-candidate-draft

This script never commits, never pushes, and never stages with git add -A.
It copies only the audited v6 include set and leaves runtime result directories
and historical raw benchmark data out of the candidate worktree.
EOF
}

mode="dry-run"
while [ "$#" -gt 0 ]; do
  case "$1" in
    --dry-run)
      mode="dry-run"
      ;;
    --apply)
      mode="apply"
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
  shift
done

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
source_root="${EULERPILOT_SOURCE_ROOT:-$(cd -- "$script_dir/.." && pwd)}"
candidate_root="${EULERPILOT_CANDIDATE_ROOT:-/root/EulerPilot-candidate}"
candidate_branch="${EULERPILOT_CANDIDATE_BRANCH:-closeout/v6-candidate-draft}"

if ! git -C "$source_root" rev-parse --show-toplevel >/dev/null 2>&1; then
  echo "source root is not a git repository: $source_root" >&2
  exit 1
fi

base_head="$(git -C "$source_root" rev-parse HEAD)"
include_list="$(mktemp)"
trap 'rm -f "$include_list"' EXIT

cat >"$include_list" <<'EOF'
.github/workflows/ci.yml
README.md
agent/include/eulerpilot.hpp
agent/include/psi_gate.hpp
agent/skills/cgroup_control/README.md
agent/src/builtin_skills/common.hpp
agent/src/builtin_skills/network_policy.cpp
agent/src/builtin_skills/policy_engine.cpp
agent/src/builtin_skills/resource_control.cpp
agent/src/builtin_skills/security_policy.cpp
agent/src/executors.cpp
agent/src/main.cpp
agent/src/psi_gate.cpp
bench/mixed/run_mixed_adaptive_closure.sh
bench/nginx/run_nginx_sched_ext_compare.sh
bench/nginx/run_nginx_sched_ext_smoke.sh
bench/psi/run_gate_mode_smoke.sh
bench/psi/run_loader_wiring_smoke.sh
bench/psi/run_psi_agent_smoke.sh
bench/redis/run_redis_sched_ext_compare.sh
bench/redis/run_redis_sched_ext_smoke.sh
bench/redis/run_static_vs_agent_compare.sh
bench/throughput/run_throughput_first_benchmark.sh
bpf/security_policy.bpf.c
configs/agent-metrics.yaml
configs/agent.yaml
configs/final_evidence_manifest.json
configs/policy_engine_security_network_resource.yaml
docs
evidence/evidence_status_overrides.json
reports/final_evidence_compact.json
reports/final_evidence_compact.md
reports/v6_dirty_worktree_intake_20260726.md
reports/v6_candidate_file_manifest_20260726.md
reports/v6_candidate_worktree_status_20260726.md
reports/v6_preflight_status_20260726.md
reports/gates/v6-preflight-20260726-111854
sched/README.md
sched/scx_eulerpilot.bpf.c
sched/scx_eulerpilot.c
scripts/README.md
scripts/build_formal_artifact.py
scripts/build_scx_eulerpilot.sh
scripts/collect_final_evidence.py
scripts/collect_scx_stats.py
scripts/create_release_bundle.py
scripts/final_quality_gate.sh
scripts/formal_artifact_gate.py
scripts/prepare_v6_candidate_worktree.sh
scripts/rollback.sh
scripts/setup_cgroup_v2.sh
scripts/v6_preflight_readiness.sh
scripts/verify_release_bundle.py
submission
tests/integration/test_benchmark_release_semantics.sh
tests/integration/test_config_validation.sh
tests/integration/test_evidence_validation_fixtures.sh
tests/integration/test_network_policy_cgroup_ownership.sh
tests/integration/test_policy_engine_transaction_model.sh
tests/integration/test_resource_control_rollback_model.sh
tests/integration/test_scx_loader_ownership.sh
tests/integration/test_security_policy_fail_closed.sh
web_console/backend/test/jobs.test.js
EOF

echo "source_root=$source_root"
echo "candidate_root=$candidate_root"
echo "candidate_branch=$candidate_branch"
echo "base_head=$base_head"
echo "mode=$mode"

missing=0
while IFS= read -r path; do
  [ -z "$path" ] && continue
  if [ ! -e "$source_root/$path" ]; then
    echo "missing include path: $path" >&2
    missing=$((missing + 1))
  fi
done <"$include_list"

if [ "$missing" -ne 0 ]; then
  echo "candidate include list has $missing missing path(s)" >&2
  exit 1
fi

echo "include_count=$(grep -cve '^[[:space:]]*$' "$include_list")"
echo "excluded_patterns=reports/events results/network_policy results/policy_engine results/resource_control results/final"

if [ "$mode" = "dry-run" ]; then
  echo "dry-run complete; no worktree was created or modified"
  exit 0
fi

if [ -e "$candidate_root" ]; then
  echo "candidate root already exists; refusing to overwrite: $candidate_root" >&2
  exit 1
fi

git -C "$source_root" worktree add -b "$candidate_branch" "$candidate_root" "$base_head"

(
  cd "$source_root"
  tar -cf - -T "$include_list"
) | (
  cd "$candidate_root"
  tar -xf -
)

chmod +x \
  "$candidate_root/scripts/prepare_v6_candidate_worktree.sh" \
  "$candidate_root/scripts/v6_preflight_readiness.sh" \
  "$candidate_root/tests/integration/test_benchmark_release_semantics.sh" \
  "$candidate_root/tests/integration/test_config_validation.sh" \
  "$candidate_root/tests/integration/test_evidence_validation_fixtures.sh" \
  "$candidate_root/tests/integration/test_network_policy_cgroup_ownership.sh" \
  "$candidate_root/tests/integration/test_policy_engine_transaction_model.sh" \
  "$candidate_root/tests/integration/test_resource_control_rollback_model.sh" \
  "$candidate_root/tests/integration/test_scx_loader_ownership.sh" \
  "$candidate_root/tests/integration/test_security_policy_fail_closed.sh"

echo "candidate worktree prepared: $candidate_root"
echo "review with:"
echo "  git -C '$candidate_root' status --short"
echo "  git -C '$candidate_root' diff --stat"
