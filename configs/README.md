# configs

作用：存放 EulerPilot Agent、策略、Skill 和实验配置。

## 关键文件

- `agent.yaml`：Agent 主配置入口。
- `skills.yaml`：当前 Skill 启停和参数配置。
- `policy.yaml`：策略相关配置。
- `psi_gate.yaml`：PSI 门控配置。
- `agent-metrics.yaml`：指标导出相关配置。

## 当前完成状态

- 已有 v1 配置可以驱动当前 Agent 和 Skills。
- v2.1 计划要求升级到 `schema_version: 2`，并统一表达：
  - `targets`
  - `network_policy.rules`
  - `security_policy.rules`
  - `resource_control.controllers`
  - `policy_engine.rules`

## 维护规则

- 配置语义变更必须同步更新 `docs/skills_yaml_plan.md`。
- 新增配置项必须说明默认值、安全边界和是否允许 enforce。
- 涉及 Network/Security/Resource 的配置必须有 target 限定，默认只允许 lab 目标。
