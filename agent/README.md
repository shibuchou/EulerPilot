# agent

作用：用户态 EulerPilot Agent 的核心实现目录。

## 关键子目录

- `include/`：Agent 公共头文件、Skill 接口、运行上下文和执行器声明。
- `src/`：Agent 主程序、Runtime、Skill 注册、SkillManager、执行器和指标导出实现。
- `observer/`：用户态观测辅助代码，例如 PSI 读取。
- `skills/`：按能力划分的 Skill 说明目录。

## 当前完成状态

- 已有 `Skill / SkillRegistry / SkillManager / builtin_skills` 基础闭环。
- 已有 `resource_control / psi_gate / network_policy_demo / security_policy_demo` 进入统一 Agent 管理。
- 下一步按 `docs/next_phase_plan_v2_1.md` 增加公共控制面：
  - `TargetResolver`
  - `AuditBus`
  - `ActionJournal`
  - `CapabilityDetector`

## 维护规则

- 新增 Runtime 级能力时，必须同步更新 `agent/skills/README.md` 和对应 Skill 子目录 README。
- 有副作用的 Skill 必须接入 rollback/status/audit。
- 不能把 demo 级能力直接写成最终完成态，需要在 docs 和 tests 中给出证据。
