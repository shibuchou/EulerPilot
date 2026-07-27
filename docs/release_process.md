# EulerPilot Release 流程

本流程用于最终交付收口，防止正式实验、最终报告和 tag 之间出现不可追溯的 SHA 自引用问题。

## 交付矩阵

- SP4/123：主验证和性能实验环境，负责 sched_ext/scx、Web Console、完整集成和 release candidate 生成。
- SP3/121：比赛要求的强制兼容交付环境，负责发行内核构建、cgroup v2 主闭环、安全扩展 smoke、safe doctor、rollback 和 sched_ext unavailable graceful fallback。
- 122：历史 OLK/sched_ext 对照，不作为最终 release 来源。

## 双 SHA 与 artifact 模型

- `tested_code_commit`：实际用于正式实验的冻结代码提交。
- `tested_candidate_commit`：完成 preflight 后形成、等待正式门禁绑定验证的候选源码提交。
- `release_candidate_commit`：等待双环境门禁确认的候选提交。
- `release_commit`：双环境门全部通过后确定的最终提交。
- `tag_commit`：最终 tag 解析到的提交，通常等于 `release_commit`。
- `artifact_id`：由 tested commit、core tree hash、build system hash、编译器/链接参数、依赖版本、kernel headers、BTF、容器或 host build manifest、构建环境白名单等输入计算的不可变产物 ID。

正式实验必须从 `tested_code_commit` 创建的 clean 只读工作树运行。实验期间不得修改 `agent/`、`bpf/`、`sched/`、核心配置和 benchmark 执行逻辑。

## 门禁分层

1. **Pre-candidate Readiness**：允许来自尚未提交的开发工作树，用于确认代码基本就绪；必须记录 `preflight_head`、`preflight_worktree_dirty` 和 `preflight_diff_sha256`，不得作为最终 tested commit 证据。
2. **Candidate-bound Gates**：Portable CI、SP4 smoke、SP3 compatibility dry-run 必须全部检出并验证同一个 `tested_candidate_commit`。任一失败都必须生成新的 candidate commit。
3. **Formal Artifact Gate**：从通过 Candidate Gate 的 `tested_code_commit` out-of-tree clean build，生成不可覆盖的 `/root/eulerpilot-artifacts/<tested_code_commit>/<artifact_id>/`，并用该 artifact 完成 RUNS=1 SCX、Throughput、Mixed-Adaptive、hash、加载卸载和残留校验。
4. **Release Gate**：正式实验完成后只允许修改报告、证据索引、release 元数据和答辩材料。`tested_code_commit..release_candidate_commit` 的核心执行路径必须等价，否则正式实验失效。

## 推荐顺序

1. 完成 P0/P1 正确性、安全修复、配置消费、benchmark baseline、Security fail-closed 和 cpuset 安全关闭口径。
2. 运行 Pre-candidate Readiness，并记录 dirty 状态与 diff hash。
3. 按 `reports/v6_dirty_worktree_intake_20260726.md` 和 `reports/v6_candidate_file_manifest_20260726.md` 精选接管文件；禁止用 `git add -A` 自动纳入 SP4 运行副产物、旧正式实验原始数据或未审核临时文件。
4. 提交计划内修改，得到 `tested_candidate_commit`，确认工作树 clean。
5. 对同一 candidate SHA 运行 Portable CI Candidate Gate、SP4 Candidate Gate 和 SP3 Candidate Gate。
6. 全部 Candidate Gate 通过后，将同一 SHA 确认为 `tested_code_commit`。
7. 从 `tested_code_commit` 的 frozen worktree 执行 out-of-tree formal build，生成 `artifact_id` 和 build manifest。
8. 对选定 formal artifact 执行 Formal Artifact Gate；失败 artifact 保留历史记录，不覆盖。
9. Formal Artifact Gate 通过后再运行正式随机化实验，输出写入仓库外目录，例如 `/root/eulerpilot-runs/<tested_code_commit>/`。
10. 只接管审核后的结果、图表、报告、evidence 索引和 release 元数据。
11. 形成 `release_candidate_commit`，检查核心代码等价。
12. 运行 `--validate-release`、SP4 final gate 和 SP3 final gate。
13. 全部通过后确认 `release_commit`。
14. 在仓库外生成 release bundle，校验源码包、manifest、evidence、PDF、视频引用和 SHA256。
15. 从源码包解压后重新构建。
16. 用户明确确认后再创建 annotated final tag。

未经用户明确要求，不 push 分支、不 push tag、不创建远端 Release，也不覆盖 `origin/main`。

## Pre-candidate Readiness

在 SP4 closeout 工作树中执行：

```bash
EULERPILOT_GATE_SMOKE_ROUNDS=1 \
EULERPILOT_GATE_DOCTOR_ROUNDS=1 \
bash scripts/v6_preflight_readiness.sh
```

脚本输出目录为 `reports/gates/v6-preflight-<timestamp>/`，其中包含 `preflight_meta.env`、`git_status_short.txt`、`git_diff_stat.txt`、各步骤日志和 `summary.env`。

该检查允许来自 dirty 工作树，只用于判断是否接近创建 `tested_candidate_commit`。它不能替代 Portable CI Candidate Gate、SP4 Candidate Gate、SP3 Candidate Gate，也不能作为 formal artifact 或正式实验放行依据。

候选工作树准备入口：

```bash
bash scripts/prepare_v6_candidate_worktree.sh --dry-run

# 用户确认后才允许执行；该命令只创建 candidate worktree，不提交、不 push。
bash scripts/prepare_v6_candidate_worktree.sh --apply
```

该脚本只复制 `reports/v6_candidate_file_manifest_20260726.md` 中定义的已审核文件，不会自动接管 `reports/events/`、`results/network_policy/`、`results/policy_engine/`、`results/resource_control/` 或旧 `results/final/*20260724-tested-2541464*` 原始数据。

## 生成 bundle

Formal artifact 构建与门禁示例：

```bash
# 在 tested_code_commit 的 clean frozen worktree 中执行。
python3 scripts/build_formal_artifact.py \
  --tested-code-commit <tested_code_commit>

python3 scripts/formal_artifact_gate.py \
  --manifest /root/eulerpilot-artifacts/<tested_code_commit>/<artifact_id>/manifests/build_manifest.json \
  --live \
  --output-json /root/eulerpilot-runs/<tested_code_commit>/formal_artifact_gate.json
```

不带 `--live` 的 `formal_artifact_gate.py` 只能做 artifact provenance/hash 预检查，不得作为正式实验放行依据。

Release bundle 示例：

```bash
python3 scripts/create_release_bundle.py \
  --version 0.9.0-rc1 \
  --tested-code-commit <tested_code_commit> \
  --release-candidate-commit <release_candidate_commit> \
  --release-commit <release_commit> \
  --artifact-manifest /root/eulerpilot-artifacts/<tested_code_commit>/<artifact_id>/manifests/build_manifest.json \
  --tag v0.9.0-rc1 \
  --output-root /root/eulerpilot-release
```

校验：

```bash
python3 scripts/verify_release_bundle.py /root/eulerpilot-release/eulerpilot-0.9.0-rc1
```

如果 bundle 校验失败，停止发布。若 tag 尚未公开，可以删除本地错误 tag 后修复；若 tag 已公开，必须使用新版本号，不得强制移动正式 tag。
