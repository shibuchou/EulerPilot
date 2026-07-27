# EulerPilot v6 Candidate Worktree Status - 2026-07-26

本文件记录 SP4 上候选预备工作树的创建与复核状态。它不是
`tested_candidate_commit`，也不是 Candidate-bound Gate 证据。

## Worktree

- 源工作树：`/root/EulerPilot-closeout`
- 候选预备工作树：`/root/EulerPilot-candidate`
- 源分支：`closeout/sp4-sp3-release`
- 候选分支：`closeout/v6-candidate-draft`
- 基线 HEAD：`f103bc61036a25a138b7f68d1d91ddf1dcf10bb0`
- 创建入口：`scripts/prepare_v6_candidate_worktree.sh --apply`

## 复核结果

```text
candidate_created=false
candidate_worktree_prepared=true
candidate_commit_state=pending_at_report_write_time
candidate_branch_pushed=false
forbidden_paths_in_candidate_status=none
include_dry_run=pass
candidate_worktree_preflight=pass
candidate_worktree_preflight_dir=/root/EulerPilot-candidate/reports/gates/v6-preflight-20260726-111854
candidate_worktree_preflight_finished_at=2026-07-26T11:19:47+08:00
```

已确认候选预备工作树的 `git status --short` 不包含以下路径：

```text
results/final/
reports/events/
results/network_policy/
results/policy_engine/
results/resource_control/
```

## 边界说明

- `/root/EulerPilot-closeout` 原 dirty 工作树未 reset、未 clean、未覆盖。
- `/root/EulerPilot-candidate` 只用于后续生成干净 candidate commit。
- 本报告写入时还没有创建 `tested_candidate_commit`；创建后必须对同一 SHA 运行 Candidate-bound Gates。
- 当前没有执行 Candidate-bound Gate。
- 当前没有生成 formal artifact。
- 当前没有运行正式随机化实验。
- 当前没有 push 分支、tag 或 release。

候选工作树中的缩短版 preflight 已通过：

```text
shell_syntax_check=pass
python_static_check=pass
docs_stale_phrase_check=pass
collect_final_evidence=pass
final_quality_gate=pass
web_console_check=pass
stage_status=PASS_WITH_LIMITATIONS
```

说明：该结果仍属于 Pre-candidate Readiness，不能替代 Candidate-bound Gates。

## 下一步

1. 在 `/root/EulerPilot-candidate` 中复查 `git diff --stat` 和关键 diff。
2. 必要时从 `/root/EulerPilot-closeout` 继续精选同步已审核文件。
3. 运行 candidate 工作树内的 preflight。
4. 用户确认后才创建 `tested_candidate_commit`。
5. 对同一个 candidate SHA 运行 Candidate-bound Gates。
