# configs

作用：存放 EulerPilot Agent、策略、Skill 和实验配置。

## 关键文件

- `agent.yaml`：Agent 主配置入口。
- `skills.yaml`：当前 Skill 启停和参数配置。
- `policy.yaml`：策略相关配置。
- `psi_gate.yaml`：PSI 门控配置。
- `agent-metrics.yaml`：指标导出相关配置。

## 当前完成状态

- `skills.yaml` 已升级到 `schema_version: 2`，当前 Agent 仍兼容旧 `schema_version: 1` flat config。
- `skills.yaml` 中 `network_policy`、`network_qos`、`network_policy_demo` 和 `security_policy_demo` 默认均为 disabled。
- `network_qos` 默认目标为 lab veth `ep-veth-qos0`，不能作为真实业务网卡默认配置使用。
- 当前 `schema_version: 2` 已覆盖：
  - `targets`
  - `rules`
  - `target_ref`
  - `network_policy` 的 `cgroup_connect4`
  - `network_qos` 的 `tc_egress`
- 后续还需要补：
  - `security_policy.rules`
  - `resource_control.controllers`
  - `policy_engine.rules`

## 维护规则

- 配置语义变更必须同步更新 `docs/skills_yaml_plan.md`。
- 新增配置项必须说明默认值、安全边界和是否允许 enforce。
- 涉及 Network/Security/Resource 的配置必须有 target 限定，默认只允许 lab 目标。
