# EulerPilot 进度状态看板

更新时间：`2026-06-18`

当前执行口径：`docs/next_phase_plan_v2_1.md`

## 当前阶段

阶段 A：公共基础设施与仓库基线，状态：`进行中`

目标：

- 修复 121 远端 Git 仓库态，建立可提交、可追踪的基线。
- 补齐 `.gitignore`、`.gitattributes` 和文档规则。
- 固定 `TargetResolver / AuditBus / ActionJournal / CapabilityDetector` 的接口与落地路径。
- 补齐项目自有顶层目录 README，让后续实现和验收位置清晰。

## 阶段完成情况

| 阶段 | 状态 | 当前结论 | 主要证据 |
|------|------|----------|----------|
| A. 公共基础设施 | 进行中 | 远端 Git、文档规则、README 覆盖、公共控制面最小代码和现有质量门禁已完成；下一步进入公共控制面深度接入 | `AGENTS.md`、本文件、各目录 README、`docs/public_control_plane_design.md`、`reports/final_quality_gate_20260618_control_plane.log` |
| B. Network Policy | 未开始 | 等待公共控制面和 target 模型完成 | `docs/next_phase_plan_v2_1.md` |
| C. Security Agent | 未开始 | 等待公共控制面、AuditBus 和 target filter | `docs/next_phase_plan_v2_1.md` |
| D. Resource Control | 未开始 | 需要扩展到 CPU + Memory 自动闭环，IO 可演示可回滚 | `docs/next_phase_plan_v2_1.md` |
| E. SP4/sched_ext 复核 | 未开始 | 等待 SP4/123 环境 | `docs/next_phase_plan_v2_1.md` |
| F. Kubernetes 与跨 Agent 联动 | 未开始 | 等待 Network/Security/Resource 正式 Skill | `docs/next_phase_plan_v2_1.md` |
| G. Benchmark 与冻结材料 | 未开始 | 等待正式能力完成 | `docs/next_phase_plan_v2_1.md` |

## 当前已完成基线

- 赛题宣讲资料已独立为 `docs/contest_briefing_reference.md`。
- 下一阶段争奖计划已升级为 `docs/next_phase_plan_v2_1.md`。
- `docs/skills_yaml_plan.md` 已升级为 v2.1 YAML/Skill 控制面规划。
- 121/122 已有 `.gitignore` 与 `.gitattributes`。
- 本地镜像已从 121 重新同步并建立 Git 初始提交。
- 121 已初始化真实 Git 仓库，当前基线提交为 `d81f920 Initialize EulerPilot remote baseline`。
- 项目自有关键顶层目录 README 已补齐：`agent/`、`bench/`、`bpf/`、`configs/`、`dashboard/`、`demo/`、`docs/`、`reports/`、`results/`、`sched/`、`scripts/`、`tools/`。
- 121 现有质量门禁已复测通过，日志为 `reports/final_quality_gate_20260618_stage_a.log`。
- 公共控制面最小代码已落地：
  - `agent/include/capability_detector.hpp` / `agent/src/capability_detector.cpp`
  - `agent/include/target_resolver.hpp` / `agent/src/target_resolver.cpp`
  - `agent/include/audit_bus.hpp` / `agent/src/audit_bus.cpp`
  - `agent/include/action_journal.hpp` / `agent/src/action_journal.cpp`
- `--doctor-skills` 已输出 Capability Detector 摘要，覆盖 BTF、BPF LSM、XDP、TC、cgroup v2、PSI、sched_ext、memory.reclaim、Kubernetes 等能力。
- 公共控制面改动后的 Agent smoke 已通过，日志为 `reports/agent_smoke_20260618_control_plane.log`。
- 公共控制面改动后的完整质量门禁已通过，日志为 `reports/final_quality_gate_20260618_control_plane.log`。

## 阶段 A 待办

| 编号 | 任务 | 状态 | 说明 |
|------|------|------|------|
| A1 | 121 初始化 Git 仓库 | 已完成 | 远端基线提交：`d81f920 Initialize EulerPilot remote baseline` |
| A2 | 关键目录 README 补齐 | 已完成 | 项目自有关键顶层目录均已有 README |
| A3 | 文档规则写入 `AGENTS.md` | 已完成 | 已要求阶段收口同步更新文档、README 和进度看板 |
| A4 | 公共控制面接口与最小实现 | 已完成 | 设计文档：`docs/public_control_plane_design.md`；`CapabilityDetector` 已接入 `--doctor-skills`，其余三项已提供可编译最小接口 |
| A5 | 质量门禁基线复测 | 已完成 | `reports/final_quality_gate_20260618_stage_a.log`，12 项 P0 与 optional checks 均通过 |

## 阶段 A 剩余工程化增强

这些内容不阻塞阶段 B 开始，但应在 Network/Security/Resource 正式接入时继续完善：

- `TargetResolver` 接入 v2 YAML 的 `targets` 配置解析。
- `AuditBus` 接入 Network/Security/Resource 事件输出。
- `ActionJournal` 接入 cgroup、BPF link、TC/XDP 动作回滚。
- `CapabilityDetector` 输出并入未来 `--status --json`。

## 进度维护规则

- 每完成阶段或关键子任务，必须更新本文件。
- 每新增目录，必须同时新增该目录的 `README.md`。
- 每个实验结果目录需要包含生成命令、环境、关键指标和结论边界。
- 每个正式 Skill 需要有独立设计文档、README、演示脚本和集成测试入口。
