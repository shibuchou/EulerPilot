# EulerPilot Changelog

## 0.9.0-rc1 - 2026-07-23

### Changed

- 明确采用 SP4 主验证与 SP3 强制兼容的双层交付矩阵。
- 增加 release candidate、tested code commit、release commit 的双 SHA 证据模型。
- 增强 SCX DSQ、Observer、cgroup、XDP、TC、PolicyEngine、doctor 和 Web Console 的安全收口约束。

### Verification

- SP4 closeout worktree host gate dry-run 通过。
- SP3 compatibility gate 在 openEuler 24.03 LTS SP3 上通过，sched_ext unavailable graceful fallback 生效。

### Pending

- 正式随机化性能实验尚未在冻结 `tested_code_commit` 后执行。
- 最终 PPT、PDF 技术报告、PDF 答辩材料和精简演示视频仍需在 release 前补齐。
