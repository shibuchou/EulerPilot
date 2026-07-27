# EulerPilot v6 Candidate File Manifest - 2026-07-26

本清单用于 `tested_candidate_commit` 创建前的文件边界确认。它不是 Git
staging 结果，也不是 Candidate-bound Gate 证据。实际提交前仍必须重新运行
`git status --short`、`git diff --stat` 和 `scripts/v6_preflight_readiness.sh`。

## 当前阶段

- 阶段：v6 Pre-candidate Readiness
- 本地镜像：`D:\code\Ubuntu\codex\eulerpilot-closeout-push`
- SP4 closeout 工作树：`192.168.1.123:/root/EulerPilot-closeout`
- SP4 分支：`closeout/sp4-sp3-release`
- 当前基线 HEAD：`f103bc61036a25a138b7f68d1d91ddf1dcf10bb0`
- 相关接管报告：`reports/v6_dirty_worktree_intake_20260726.md`

## Candidate Include

以下内容属于候选提交应接管范围。

### 代码与配置

- `.github/workflows/ci.yml`
- `agent/include/eulerpilot.hpp`
- `agent/include/psi_gate.hpp`
- `agent/src/builtin_skills/common.hpp`
- `agent/src/builtin_skills/network_policy.cpp`
- `agent/src/builtin_skills/policy_engine.cpp`
- `agent/src/builtin_skills/resource_control.cpp`
- `agent/src/builtin_skills/security_policy.cpp`
- `agent/src/executors.cpp`
- `agent/src/main.cpp`
- `agent/src/psi_gate.cpp`
- `bpf/security_policy.bpf.c`
- `sched/scx_eulerpilot.c`
- `sched/scx_eulerpilot.bpf.c`
- `configs/agent.yaml`
- `configs/agent-metrics.yaml`
- `configs/policy_engine_security_network_resource.yaml`

### Benchmark 与脚本

- `bench/mixed/run_mixed_adaptive_closure.sh`
- `bench/nginx/run_nginx_sched_ext_compare.sh`
- `bench/nginx/run_nginx_sched_ext_smoke.sh`
- `bench/psi/run_gate_mode_smoke.sh`
- `bench/psi/run_loader_wiring_smoke.sh`
- `bench/psi/run_psi_agent_smoke.sh`
- `bench/redis/run_redis_sched_ext_compare.sh`
- `bench/redis/run_redis_sched_ext_smoke.sh`
- `bench/redis/run_static_vs_agent_compare.sh`
- `bench/throughput/run_throughput_first_benchmark.sh`
- `scripts/build_scx_eulerpilot.sh`
- `scripts/collect_final_evidence.py`
- `scripts/collect_scx_stats.py`
- `scripts/create_release_bundle.py`
- `scripts/final_quality_gate.sh`
- `scripts/rollback.sh`
- `scripts/setup_cgroup_v2.sh`
- `scripts/verify_release_bundle.py`
- `scripts/build_formal_artifact.py`
- `scripts/formal_artifact_gate.py`
- `scripts/prepare_v6_candidate_worktree.sh`
- `scripts/v6_preflight_readiness.sh`

### 测试

- `tests/integration/test_benchmark_release_semantics.sh`
- `tests/integration/test_config_validation.sh`
- `tests/integration/test_evidence_validation_fixtures.sh`
- `tests/integration/test_network_policy_cgroup_ownership.sh`
- `tests/integration/test_policy_engine_transaction_model.sh`
- `tests/integration/test_resource_control_rollback_model.sh`
- `tests/integration/test_scx_loader_ownership.sh`
- `tests/integration/test_security_policy_fail_closed.sh`
- `web_console/backend/test/jobs.test.js`

### 文档、证据覆盖和 preflight 记录

- `README.md`
- `docs/`
- `submission/`
- `agent/skills/cgroup_control/README.md`
- `sched/README.md`
- `scripts/README.md`
- `configs/final_evidence_manifest.json`
- `evidence/evidence_status_overrides.json`
- `reports/final_evidence_compact.json`
- `reports/final_evidence_compact.md`
- `reports/v6_dirty_worktree_intake_20260726.md`
- `reports/v6_preflight_status_20260726.md`
- `reports/v6_candidate_file_manifest_20260726.md`
- `reports/v6_candidate_worktree_status_20260726.md`
- `reports/gates/v6-preflight-20260726-111854/`

## Candidate Exclude

以下内容不得自动进入 `tested_candidate_commit`。

### SP4 运行副产物

- `reports/events/network_policy.jsonl`
- `results/network_policy/cgroup-ownership-*/`
- `results/policy_engine/security-resource-*/`
- `results/policy_engine/security-network-resource-*/`
- `results/resource_control/integration-*/`

处理原则：

- 这些目录保留在 SP4 原工作树中作为运行记录。
- 若后续要作为 evidence，必须先选定单个语义完整目录，补齐命令、HEAD、
  host/kernel、cleanup、SHA256 和 manifest 后再接管。
- 不允许因 `git add -A` 把所有运行结果直接带入候选提交。

### 旧正式实验原始数据

- `results/final/*20260724-tested-2541464*`

处理原则：

- 当前旧性能结果按 v6 规则是 historical/provisional/invalid。
- 旧 JSON、CSV、trace、日志保持原样，不直接改写。
- 当前工作树中这些 CSV 被 Git 标记为 modified，但 `git diff --numstat -- results/final`
  没有内容变更，仅表现为换行状态提示；候选提交前应排除这些路径。
- 引用资格由 `evidence/evidence_status_overrides.json` 控制。

## 提交前强制复查

创建 `tested_candidate_commit` 前必须完成：

```bash
git status --short
git diff --stat
git diff --numstat -- results/final
bash scripts/v6_preflight_readiness.sh
bash scripts/prepare_v6_candidate_worktree.sh --dry-run
```

期望：

- `results/final/*20260724-tested-2541464*` 不进入候选提交。
- SP4 未跟踪运行结果目录不进入候选提交。
- 候选提交只包含已审核代码、测试、文档、metadata 和 preflight 记录。
- candidate 创建后再对同一 SHA 运行 Candidate-bound Gates。
