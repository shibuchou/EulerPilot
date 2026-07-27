# EulerPilot v6 Preflight Status - 2026-07-26

本记录是 `tested_candidate_commit` 创建前的开发检查状态，不是 Candidate-bound Gate，
也不是正式性能实验或 release 证据。

## 当前仓库

- 主验证仓库：`192.168.1.123:/root/EulerPilot-closeout`
- 分支：`closeout/sp4-sp3-release`
- 当前 HEAD：`f103bc61036a25a138b7f68d1d91ddf1dcf10bb0`
- 阶段：v6 Pre-candidate Readiness
- 工作区：dirty，保留用于分阶段接管和审计；尚未创建 `tested_candidate_commit`

## 本轮已完成

### SecurityPolicy fail-closed

- 增强 `tests/integration/test_security_policy_fail_closed.sh`。
- 覆盖：
  - audit path 规则可校验。
  - enforce path 规则缺少 cgroup scope 时拒绝。
  - TCP socket audit 规则可校验。
  - UDP socket 规则拒绝为 `security-policy-v2-socket-protocol-unavailable`。
  - TCP socket enforce 缺少 cgroup scope 时拒绝。
  - BPF 程序必须读取 `sk_protocol`，且协议不可用时 fail closed。
- 已纳入 `scripts/final_quality_gate.sh`。

### Config consumption audit

- 扩展 `tests/integration/test_config_validation.sh`。
- 已显式覆盖并拒绝历史遗留但未消费字段：
  - `agent.fallback_enabled`
  - `observer.ebpf.enabled`
  - `observer.ebpf.collect_wait_ns`
  - `scheduler.default_profile`
  - `scheduler.name`
  - `scheduler.enable_rollback`
- `agent.name` 仍为真实消费字段，用于 Agent 标题输出。

### Evidence validation fixtures

- `tests/integration/test_evidence_validation_fixtures.sh` 已纳入 `scripts/final_quality_gate.sh`。
- 覆盖：
  - throughput 正向 fixture 可通过。
  - throughput enqueue batch 但无 class-aware dispatch 的负向 fixture 必须失败。
  - mixed-adaptive 单 Agent 有序状态链正向 fixture 可通过。
  - mixed-adaptive 跨 agent instance 拼接负向 fixture 必须失败。

### Web Console job lifecycle

- 扩展 `web_console/backend/test/jobs.test.js`。
- 覆盖：
  - `cleanup_action` 在 mutating job 失败后真实执行。
  - cancel 后进程退出不会把 job 状态覆盖为 failed。
- 继续保持 Web Console 旁路控制台边界，不修改 Agent 主干。

## 已运行检查

在 SP4 closeout 工作树执行：

```bash
bash tests/integration/test_security_policy_fail_closed.sh
bash tests/integration/test_config_validation.sh
bash tests/integration/test_evidence_validation_fixtures.sh
python3 -m py_compile scripts/build_formal_artifact.py scripts/formal_artifact_gate.py scripts/collect_final_evidence.py
python3 scripts/collect_final_evidence.py
EULERPILOT_GATE_SMOKE_ROUNDS=1 EULERPILOT_GATE_DOCTOR_ROUNDS=1 bash scripts/final_quality_gate.sh
cd web_console && npm test && npm run lint && npm run build
```

统一 preflight 入口也已执行：

```bash
EULERPILOT_GATE_SMOKE_ROUNDS=1 \
EULERPILOT_GATE_DOCTOR_ROUNDS=1 \
bash scripts/v6_preflight_readiness.sh
```

输出目录：

```text
reports/gates/v6-preflight-20260726-110606/
```

结果：

- `security_policy_fail_closed=pass`
- `config_consumption_audit=pass`
- `evidence_validation_fixtures=pass`
- `collect_final_evidence`: `entries=41 missing_required=0 warnings=8`
- `final_quality_gate`: `29/29` P0 checks passed，1 轮 smoke 和 1 轮 safe doctor passed
- Web Console backend tests: `12/12` passed
- Web Console lint/build: passed
- `v6_preflight_readiness`: `PASS_WITH_LIMITATIONS`
- 最新统一 preflight 时间：`2026-07-26T11:06:51+08:00`

## 仍未进入的阶段

- 尚未创建 `tested_candidate_commit`。
- 尚未执行 Candidate-bound Gates。
- 尚未确认 `tested_code_commit`。
- 尚未生成 formal artifact。
- 尚未执行 Formal Artifact Gate。
- 尚未运行正式随机化性能实验。
- 尚未形成 release candidate、release bundle 或 tag。
- 已生成 dirty 接管分类：`reports/v6_dirty_worktree_intake_20260726.md`。进入 `tested_candidate_commit` 前，SP4 上未跟踪 `results/` 与 `reports/events/network_policy.jsonl` 必须按该报告精选或排除，不能自动随源码提交。
- 已生成候选提交文件边界清单：`reports/v6_candidate_file_manifest_20260726.md`。候选提交应接管代码、测试、文档、metadata 和 preflight 记录；不得自动接管旧 `results/final/*20260724-tested-2541464*` 原始数据或 SP4 运行副产物。

## 当前 stage status

`PASS_WITH_LIMITATIONS`

原因：

- 本轮新增的安全/证据/Web/配置 preflight 检查均已通过。
- 旧性能结果仍为 provisional/historical，不能作为 final positive evidence。
- 工作区仍 dirty，必须继续按 v6 方案完成接管、preflight 汇总和 candidate-bound gate。
