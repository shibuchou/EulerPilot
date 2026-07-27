# docs

本目录保存 EulerPilot 的设计方案、阶段计划、实验结论、最终报告和答辩材料。文档更新时间为 `2026-07-26`；当前验证基线为 SP4/123 v6 收口线与 SP3/121 强制兼容线。旧 SP4 RUNS=10 结果作为 historical/provisional evidence 保留，正式收益数字等待 formal artifact 重跑；带日期的历史快照文档只作为过程记录，不再反复覆盖。

## 推荐阅读顺序

1. `one_page_summary.md`：一页式项目简介。
2. `progress_status.md`：当前滚动进度状态，包含 SP4/123、Kubernetes、Web Console、41 条 evidence 和 v6 preflight 状态。
3. `final_evidence_index.md`：最终证据索引和 `collect_final_evidence.py` / `--validate-release` 入口。
4. `final_report_submission.md`：最终报告主稿。
5. `architecture.md`：系统架构、模块边界和双执行后端说明。
6. `system_design.md`：架构图、闭环流程图和可编辑图源。
7. `demo_final_runbook.md`：现场演示流程。
8. `submission_checklist.md`：最终提交检查表。

## 核心设计文档

- `contest_briefing_reference.md`：比赛宣讲整理和完成度参考。
- `resource_control_skill.md`：Resource Control CPU/Memory/IO、target_ref、runtime/Pod target 和 rollback。
- `network_policy_skill.md`：Network Policy、TC QoS、XDP、Pod host veth 与安全边界。
- `security_policy_skill.md`：BPF LSM、syscall tracing、anomaly、credential 生命周期和 scope。
- `policy_engine_skill.md`：跨 Skill 联动、统一 transaction、失败回滚和审计链。
- `web_console_design.md`：Evidence-first + 白名单 Demo + 旁路展示控制台设计。
- `sp4_validation_plan.md`：SP4 发行环境适配和 SP4 官方源码自编译 sched_ext 内核复核。
- `sp4_k8s_validation_plan.md`：Kubernetes 旁路、隔离、可清理验证方案。

## 图示入口

- `assets/eulerpilot_architecture_board.svg`：推荐放入 README 和答辩材料的分层项目架构总览图。
- `assets/eulerpilot_architecture_detailed.drawio`：可编辑 Draw.io 架构图。
- `assets/eulerpilot_architecture_detailed.mmd`：Mermaid 架构图源。
- `assets/eulerpilot_closed_loop_flow.mmd`：Agent 观测、决策、执行、回滚闭环流程图源。

图示中的环境口径必须保持一致：

```text
SP3 是 cgroup v2 稳定主路径；
OLK-6.6 是 sched_ext/scx 对照验证线；
SP4 发行环境已完成适配验证；
sched_ext/scx 基于 SP4 官方源码自编译启用内核完成复核；
不声称 SP4 发行默认内核直接支持 sched_ext。
```

## 历史计划与快照

- `current_completion_report_20260629.md`、`v3_1_start_status_20260629.md` 等文件是历史快照，保留当时口径。
- `next_phase_plan_v1.md`、`next_phase_plan_v2.md`、`next_phase_plan_v2_1.md`、`next_phase_plan_v3_2.md` 是阶段计划归档。
- 当前交付状态请看 `progress_status.md`、`final_evidence_index.md` 和 `submission_checklist.md`。

## 维护规则

- 首页和滚动状态文档优先反映 SP4/123 最新主验证线。
- 历史快照文档不得静默改写成当前状态。
- 新增正式 Skill 或关键验证路径时，必须补独立设计文档、测试入口、结果目录和 rollback/cleanup 说明。
- 不把未完成能力写成已完成；Kubernetes/真实 runtime 结果必须标注隔离 namespace、label、资源限制和 cleanup 状态。
