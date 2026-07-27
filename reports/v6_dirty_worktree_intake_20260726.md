# EulerPilot v6 Dirty Worktree Intake - 2026-07-26

本文件用于 `tested_candidate_commit` 创建前的 dirty 工作区接管分类。它不是
Candidate-bound Gate，也不是正式 release manifest。

## 基线

- 本地镜像：`D:\code\Ubuntu\codex\eulerpilot-closeout-push`
- 本地分支：`fix/v6-closeout-blockers`
- SP4 closeout：`192.168.1.123:/root/EulerPilot-closeout`
- SP4 分支：`closeout/sp4-sp3-release`
- SP4 当前 HEAD：`f103bc61036a25a138b7f68d1d91ddf1dcf10bb0`
- 最新 preflight：`reports/gates/v6-preflight-20260726-110606/`

## 状态摘要

| 位置 | tracked diff | untracked | untracked results | 说明 |
|---|---:|---:|---:|---|
| 本地 closeout 镜像 | 75 | 21 | 0 | 本地只保留已同步的源码、文档、preflight 目录和新增测试脚本 |
| SP4 closeout 工作树 | 75 | 313 | 291 | SP4 含多轮集成测试结果目录，需单独审核，不得自动纳入候选提交 |

## 应纳入候选提交的正式代码

这些文件属于 v6 修复本体，计划进入 `tested_candidate_commit`：

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
- `configs/agent.yaml`
- `configs/agent-metrics.yaml`
- `configs/policy_engine_security_network_resource.yaml`
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
- `scripts/v6_preflight_readiness.sh`

## 应纳入候选提交的测试

这些测试用于锁定复审阻塞项和候选提交前语义：

- `tests/integration/test_benchmark_release_semantics.sh`
- `tests/integration/test_config_validation.sh`
- `tests/integration/test_evidence_validation_fixtures.sh`
- `tests/integration/test_network_policy_cgroup_ownership.sh`
- `tests/integration/test_policy_engine_transaction_model.sh`
- `tests/integration/test_resource_control_rollback_model.sh`
- `tests/integration/test_scx_loader_ownership.sh`
- `tests/integration/test_security_policy_fail_closed.sh`
- `web_console/backend/test/jobs.test.js`

## 应纳入候选提交的文档和元数据

这些文件用于统一当前交付口径、证据覆盖和流程边界：

- `README.md`
- `docs/*.md` 中本次 v6 口径更新文件
- `submission/*.md`
- `scripts/README.md`
- `sched/README.md`
- `agent/skills/cgroup_control/README.md`
- `configs/final_evidence_manifest.json`
- `evidence/evidence_status_overrides.json`
- `reports/final_evidence_compact.md`
- `reports/final_evidence_compact.json`
- `reports/v6_preflight_status_20260726.md`
- `reports/gates/v6-preflight-20260726-104253/`

说明：

- `reports/final_evidence_compact.*` 是由 `scripts/collect_final_evidence.py` 基于 manifest 和 status override 生成的当前 compact 入口，可以随候选提交接管。
- `reports/gates/v6-preflight-20260726-104253/` 是 Pre-candidate Readiness 记录，只证明 dirty 工作树开发检查通过，不能作为 Candidate-bound Gate 或 formal artifact 证据。

## 暂不纳入候选提交的运行副产物

以下内容应保留在 SP4 原工作树中，进入候选提交前需要人工决定是否精选、归档或排除：

- `reports/events/network_policy.jsonl`
- `results/network_policy/cgroup-ownership-20260724-*`
- `results/network_policy/cgroup-ownership-20260725-*`
- `results/network_policy/cgroup-ownership-20260726-*`
- `results/policy_engine/security-resource-20260725-*`
- `results/policy_engine/security-network-resource-20260725-*`
- `results/resource_control/integration-20260725-*`

原因：

- 它们是运行测试产生的结果目录或事件流，不是源码修复本体。
- 其中部分目录是同一测试的多次迭代结果，直接全部接管会让候选提交臃肿。
- 如果需要作为 evidence，应先选定一组最新且语义完整的结果目录，记录命令、HEAD、host/kernel、cleanup 状态和 SHA256，再加入 manifest。

## 禁止未经审核改写的历史原始数据

旧 SP4 `results/final/*20260724-tested-2541464*` 性能目录按 v6 规则保留为
historical/provisional/invalid evidence。原则：

- 不改写旧 JSON、CSV、trace、日志或实验目录。
- 通过 `evidence/evidence_status_overrides.json` 覆盖引用状态。
- 当前本地 `results/final/*.csv` 出现换行状态提示，但 `git diff --stat -- results/final` 没有实际内容 diff；进入 candidate 前仍需复查并避免提交任何旧原始结果改写。

## 进入 tested_candidate_commit 前的待办

1. 对 SP4 未跟踪 `results/` 目录做人工精选或全部排除。
2. 复查 `reports/events/network_policy.jsonl` 是否只是运行副产物；若不是正式 evidence，候选提交前不接管。
3. 再跑一次 `scripts/v6_preflight_readiness.sh`。
4. 确认 candidate 工作树只包含已审核代码、测试、文档、metadata 和 preflight 审计材料。
5. 创建 `tested_candidate_commit` 后，必须对同一 SHA 运行 Candidate-bound Gates。
