# EulerPilot 进度状态看板

更新时间：`2026-06-18`

当前执行口径：`docs/next_phase_plan_v2_1.md`

## 当前阶段

阶段 B：Network Policy 完整实现，状态：`进行中`

目标：

- 将 `network_policy_demo` 升级为正式 `network_policy` Skill。
- 先完成 `cgroup/connect4` 的 `audit/enforce/status/rollback` 正式口径。
- 再补 TC QoS 和 isolated-veth XDP。
- 所有 Network 事件接入 `AuditBus`，所有挂载/卸载动作接入 `ActionJournal`。

## 阶段完成情况

| 阶段 | 状态 | 当前结论 | 主要证据 |
|------|------|----------|----------|
| A. 公共基础设施 | 已完成 | 远端 Git、文档规则、README 覆盖、公共控制面最小代码和现有质量门禁已完成；后续随正式 Skill 深度接入 | `AGENTS.md`、本文件、各目录 README、`docs/public_control_plane_design.md`、`reports/final_quality_gate_20260618_control_plane.log` |
| B. Network Policy | 进行中 | 正式 `network_policy` 注册名已落地；connect4 audit/enforce、动态端口、命中统计和 rollback 无残留已验证；下一步进入 TC QoS 和 isolated-veth XDP | `docs/network_policy_skill.md`、`tests/integration/test_network_policy.sh`、`results/network_policy/integration-20260618-210126/` |
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
- 121 提交记录：
  - `d81f920 Initialize EulerPilot remote baseline`
  - `6219b82 Document stage A baseline progress`
  - `d7fbee6 Add stage A control plane foundation`
- 122 已同步公共控制面代码，并完成 `make -B agent` 与 `--doctor-skills` 检查；122 doctor 显示 `sched_ext` available。

## 阶段 A 任务收口

| 编号 | 任务 | 状态 | 说明 |
|------|------|------|------|
| A1 | 121 初始化 Git 仓库 | 已完成 | 远端基线提交：`d81f920 Initialize EulerPilot remote baseline` |
| A2 | 关键目录 README 补齐 | 已完成 | 项目自有关键顶层目录均已有 README |
| A3 | 文档规则写入 `AGENTS.md` | 已完成 | 已要求阶段收口同步更新文档、README 和进度看板 |
| A4 | 公共控制面接口与最小实现 | 已完成 | 设计文档：`docs/public_control_plane_design.md`；`CapabilityDetector` 已接入 `--doctor-skills`，其余三项已提供可编译最小接口 |
| A5 | 质量门禁基线复测 | 已完成 | `reports/final_quality_gate_20260618_stage_a.log`，12 项 P0 与 optional checks 均通过 |

## 阶段 B 立即任务

| 编号 | 任务 | 状态 | 说明 |
|------|------|------|------|
| B1 | NetworkPolicySkill 文档升级 | 已完成 | `docs/network_policy_skill.md` 已固定正式 Skill 目标、YAML、事件和回滚口径 |
| B2 | `network_policy_demo` 到 `network_policy` 迁移方案 | 已完成 | 正式注册名 `network_policy` 已增加，`network_policy_demo` 保留兼容 |
| B3 | connect4 audit/enforce | 已完成 | audit 模式不挂 BPF；enforce 模式使用 BPF map 动态配置端口，`stats_map` 记录 allow/deny，rollback 后无 attachment 残留 |
| B4 | TC QoS | 待开始 | 使用 eBPF TC classifier + HTB/TBF |
| B5 | isolated-veth XDP | 待开始 | 只允许 lab veth/netns，不挂生产管理网卡 |

## 阶段 B 当前证据

- 正式 `network_policy` Skill 已注册，`network_policy_demo` 保留兼容。
- `configs/skills.yaml` 已增加默认 disabled 的 `network_policy` 配置，默认模式为 `audit`。
- `tests/integration/test_network_policy.sh` 已验证：
  - `network_policy` 能被 `--list-skills` 枚举。
  - audit 模式下 `--doctor-skills` 通过。
  - audit 模式下 Agent 能运行。
  - audit 模式不挂载 cgroup BPF，不会阻断流量。
  - audit 模式会写入 `reports/events/network_policy.jsonl`。
- enforce 模式已验证：
  - YAML 中 `dst_port: '18081'` 会写入 BPF `policy_map`。
  - 目标 cgroup 内 curl 被 cgroup/connect4 拒绝，结果为 `rc=7 http_code=000`。
  - rollback 事件包含 `deny_count=1`，证明 `stats_map` 生效。
  - Agent 退出后 `/sys/fs/bpf/eulerpilot_network_policy_link` 和 cgroup BPF attachment 无残留。
- 121 最新集成测试证据目录：`results/network_policy/integration-20260618-210126/`。
- 122 最新集成测试证据目录：`results/network_policy/integration-20260618-211444/`。
- 121 完整质量门禁已通过：`reports/final_quality_gate_20260618_network_policy_dynamic.log`。

## 阶段 A 后续随阶段接入项

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
