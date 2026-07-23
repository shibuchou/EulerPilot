# EulerPilot Release 流程

本流程用于最终交付收口，防止正式实验、最终报告和 tag 之间出现不可追溯的 SHA 自引用问题。

## 交付矩阵

- SP4/123：主验证和性能实验环境，负责 sched_ext/scx、Web Console、完整集成和 release candidate 生成。
- SP3/121：比赛要求的强制兼容交付环境，负责发行内核构建、cgroup v2 主闭环、安全扩展 smoke、safe doctor、rollback 和 sched_ext unavailable graceful fallback。
- 122：历史 OLK/sched_ext 对照，不作为最终 release 来源。

## 双 SHA 模型

- `tested_code_commit`：实际用于正式实验的冻结代码提交。
- `release_candidate_commit`：等待双环境门禁确认的候选提交。
- `release_commit`：双环境门全部通过后确定的最终提交。
- `tag_commit`：最终 tag 解析到的提交，通常等于 `release_commit`。

正式实验必须从 `tested_code_commit` 创建的 clean 只读工作树运行。实验期间不得修改 `agent/`、`bpf/`、`sched/`、核心配置和 benchmark 执行逻辑。

## 推荐顺序

1. 完成 P0/P1 正确性和安全修复。
2. 运行 portable CI、SP4 host gate dry-run 和 SP3 compatibility gate dry-run。
3. 创建冻结代码提交，记录为 `tested_code_commit`。
4. 从冻结提交创建只读实验工作树。
5. 正式实验输出写入仓库外目录，例如 `/root/eulerpilot-runs/<tested_code_commit>/`。
6. 只接管审核后的结果、图表、报告、evidence 索引和 release 元数据。
7. 形成 `release_candidate_commit`。
8. 检查 `tested_code_commit..release_candidate_commit` 的核心代码 diff 是否为空或符合白名单。
9. 运行 SP4 完整门和 SP3 强制兼容门。
10. 若门禁失败，修复后生成新的 `release_candidate_commit` 并重跑受影响门禁。
11. 双环境门全部通过后，将最终候选提交确定为 `release_commit`。
12. 在仓库外生成 release bundle。
13. 校验源码包、manifest、evidence、PDF、视频引用和 SHA256。
14. 从源码包解压后重新构建。
15. 用户明确确认后再创建 annotated final tag。

未经用户明确要求，不 push 分支、不 push tag、不创建远端 Release，也不覆盖 `origin/main`。

## 生成 bundle

示例：

```bash
python3 scripts/create_release_bundle.py \
  --version 0.9.0-rc1 \
  --tested-code-commit <tested_code_commit> \
  --release-candidate-commit <release_candidate_commit> \
  --release-commit <release_commit> \
  --tag v0.9.0-rc1 \
  --output-root /root/eulerpilot-release
```

校验：

```bash
python3 scripts/verify_release_bundle.py /root/eulerpilot-release/eulerpilot-0.9.0-rc1
```

如果 bundle 校验失败，停止发布。若 tag 尚未公开，可以删除本地错误 tag 后修复；若 tag 已公开，必须使用新版本号，不得强制移动正式 tag。
