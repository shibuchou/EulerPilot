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
- `resource_control` 默认启用，已配置 CPU + Memory + IO 控制器与 `latency/batch/background` profile；写入只作用于 `/sys/fs/cgroup/eulerpilot/*` 实验 cgroup。
- `skills.yaml` 中 `network_policy`、`network_qos`、`network_xdp`、`security_policy`、`network_policy_demo` 和 `security_policy_demo` 默认均为 disabled。
- `network_qos` 默认目标为 lab veth `ep-veth-qos0`，不能作为真实业务网卡默认配置使用。
- `network_xdp` 默认目标为 lab veth `ep-veth-xdp0`，包含 ICMP drop、TCP:19092 drop、UDP:19093 drop 与 UDP tuple `10.89.0.2:39094 -> 10.89.0.1:19094` 四条 lab 规则，只能用于 isolated veth/netns 或后续 lab Pod veth。
- `security_policy` 默认目标为 demo secret 文件路径，只允许在 `/root/EulerPilot/demo/security_policy_demo/secret.txt` 上验证 audit/enforce 最小闭环；正式业务路径需要后续动态 target map 和 allowlist。
- 当前 `schema_version: 2` 已覆盖：
  - `targets`
  - `rules`
  - `target_ref`
  - `network_policy` 的 `cgroup_connect4`
  - `network_qos` 的 `tc_egress`
  - `network_xdp` 的 `xdp`
  - `security_policy` 的 `lsm_file_open` path target 最小闭环
  - `resource_control` 的 `controllers.cpu.max`、`controllers.memory.high/low/max/reclaim`、`controllers.io.weight/max/device` 与 `profiles.latency/batch/background`
- 后续还需要补：
  - `security_policy` 的动态 path/process/container target map 和 syscall tracing 规则
  - `resource_control` 的 container/Pod target 解析
  - `policy_engine.rules`

## 维护规则

- 配置语义变更必须同步更新 `docs/skills_yaml_plan.md`。
- 新增配置项必须说明默认值、安全边界和是否允许 enforce。
- 涉及 Network/Security/Resource 的配置必须有 target 限定，默认只允许 lab 目标。
