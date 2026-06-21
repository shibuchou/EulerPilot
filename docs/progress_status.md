# EulerPilot 进度状态看板

更新时间：`2026-06-21`

当前执行口径：`docs/next_phase_plan_v2_1.md`

## 当前阶段

阶段 B：Network Policy 完整实现，状态：`收尾 / Pod veth 预备`

阶段 C：Security Agent 正式化，状态：`BPF LSM + syscall tracing + 多目标 target_map + 规则级事件标识 + cgroup scope 最小闭环已完成`

目标：

- 将 `network_policy_demo` 升级为正式 `network_policy` Skill。
- 先完成 `cgroup/connect4` 的 `audit/enforce/status/rollback` 正式口径。
- 再补 TC QoS 和 isolated-veth XDP，并继续扩展 Benchmark、多规则和 Pod veth。
- 先补 `TargetResolver` 的 netdev 与 `k8s_pod` 安全诊断入口，为后续 Pod veth 真实解析和 XDP/TC 安全挂载做准备。
- 所有 Network 事件接入 `AuditBus`，所有挂载/卸载动作接入 `ActionJournal`。

## 阶段完成情况

| 阶段 | 状态 | 当前结论 | 主要证据 |
|------|------|----------|----------|
| A. 公共基础设施 | 已完成 | 远端 Git、文档规则、README 覆盖、公共控制面最小代码和现有质量门禁已完成；后续随正式 Skill 深度接入 | `AGENTS.md`、本文件、各目录 README、`docs/public_control_plane_design.md`、`reports/final_quality_gate_20260618_control_plane.log` |
| B. Network Policy | 收尾 / Pod veth 预备 | 正式 `network_policy` 注册名已落地；connect4 audit/enforce 已完成；TC QoS 最小闭环与速率误差 Benchmark 已完成；isolated-veth XDP 多规则闭环已完成；schema v2 `targets + rules + target_ref` 已落地；`TargetResolver` 已补 netdev 与 `k8s_pod` 诊断型入口；下一步实现真实 Pod sandbox/netns/veth 映射并接入 `network_qos/network_xdp` | `docs/network_policy_skill.md`、`docs/network_pod_veth_target.md`、`tests/integration/test_network_policy.sh`、`tests/integration/test_network_qos_tc.sh`、`tests/integration/test_network_xdp.sh`、`tests/integration/test_target_resolver.sh`、`tests/benchmark/test_network_qos_rate.sh` |
| C. Security Agent | 双 LSM + 四类 syscall tracing + 多目标 target_map + 规则级事件标识 + cgroup scope 最小闭环已完成 | 正式 `security_policy` 注册名已落地；YAML v2 path/exec_path/cgroup_path target、用户态下发最多 8 项 BPF `target_map`、audit BPF attach 不阻断 + `lsm_file_open/lsm_bprm_check_security/sys_enter_execve/sys_enter_openat/sys_enter_connect/sys_enter_ptrace` ringbuf observed hit、enforce BPF LSM blocked hit、双动态 `/tmp` 目标验证、LSM blocked 事件按 `target_index` 映射到单条 YAML `rule_id/target_ref`、显式 cgroup scope 内阻断且 scope 外允许、rollback 无残留已在 121/122 通过；`security_policy_demo` 保留兼容；Pod/container 自动解析尚未完成 | `docs/security_policy_skill.md`、`agent/skills/security_policy/README.md`、`tests/integration/test_security_policy.sh`、`demo/security_policy_demo/README.md` |
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
| B4 | TC QoS | 已完成最小闭环 | `network_qos` 使用 TC egress BPF classifier 统计命中，TBF qdisc 执行限速；已验证 lab netns/veth、audit/enforce 和 rollback |
| B5 | YAML v2 targets/rules | 已完成最小闭环 | `configs/skills.yaml` 已升级为 `schema_version: 2`；connect4 与 TC QoS 均通过 `target_ref` 解析目标 |
| B6 | isolated-veth XDP | 已完成多规则闭环 | `network_xdp` 使用 generic XDP 在专用 lab veth 上执行 ICMP drop 与 TCP:19092 drop；已验证 audit/enforce、多规则 drop 统计和 rollback 后连通性恢复 |
| B7 | TC QoS 速率误差 Benchmark | 已完成 | `tests/benchmark/test_network_qos_rate.sh` 使用 Python TCP rate probe 验证 2 Mbit/s TBF 限速；121 误差 -1.22%，122 误差 -1.45% |
| B8 | Pod veth target 解析预备 | 已完成诊断型入口 | `TargetResolver` 已支持 netdev ifname 校验、ifindex 解析和 `k8s_pod` reason code；`tests/integration/test_target_resolver.sh` 不依赖 Kubernetes，可验证错误路径和默认 lab namespace 安全边界 |

## 阶段 B 当前证据

- 正式 `network_policy` Skill 已注册，`network_policy_demo` 保留兼容。
- `configs/skills.yaml` 已升级为 `schema_version: 2`，`network_policy`、`network_qos` 与 `network_xdp` 均使用 `targets + rules + target_ref`。
- `network_policy` 默认 disabled，默认模式为 `audit`。
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
- 121 最新集成测试证据目录：`results/network_policy/integration-20260619-142347/`。
- 122 最新集成测试证据目录：`results/network_policy/integration-20260619-122352/`。
- 121 完整质量门禁已通过：`reports/final_quality_gate_20260621_security_cgroup_scope.log`。

### YAML v2 证据

- `SkillManager` 已支持 `schema_version: 1/2`，并将嵌套 YAML flatten 为现有 `SkillSpec.config`，避免大改 Skill 接口。
- `network_policy` 已优先读取 `rules.*.hook=cgroup_connect4` 和 `targets.<target_ref>.type=cgroup`。
- `network_qos` 已优先读取 `rules.*.hook=tc_egress` 和 `targets.<target_ref>.type=netdev`。
- `network_xdp` 已优先读取 `rules.*.hook=xdp` 和 `targets.<target_ref>.type=netdev`。
- 审计事件已带上 v2 规则和目标：
  - connect4：`rule_id=deny_demo_port`，`target_ref=demo_cgroup`。
  - TC QoS：`rule_id=limit_lab_egress`，`target_ref=lab_veth`。
  - XDP：`rule_id=drop_icmp_lab`，`target_ref=lab_xdp_veth`。
  - 121 验证事件位于 `reports/events/network_policy.jsonl`。

### TC QoS 证据

- 新增正式子能力 `network_qos`，默认 disabled。
- 新增 BPF 程序 `bpf/network_qos_tc.bpf.c`，提供 `tc_egress` classifier 命中统计。
- 新增构建目标 `make network-qos-tc`。
- 新增清理脚本 `scripts/cleanup_network_qos_tc.sh`，负责删除 lab veth/netns、root qdisc 和 clsact。
- 新增集成测试 `tests/integration/test_network_qos_tc.sh`，已验证：
  - `network_qos` 能被 `--list-skills` 枚举。
  - 专用 lab netns/veth 基线连通。
  - audit 模式不修改 TC qdisc。
  - enforce 模式安装 TC clsact + TBF。
  - ping 流量命中 BPF stats，rollback 事件记录 `packet_count=3`、`byte_count=294`。
  - Agent 退出后无 TC qdisc 残留。
- 121 最新 TC QoS 集成测试证据目录：`results/network_policy/qos-tc-20260619-142357/`。
- 122 最新 TC QoS 集成测试证据目录：`results/network_policy/qos-tc-20260619-122403/`。

### TC QoS Benchmark 证据

- 新增工具 `tools/tcp_rate_probe.py`，只依赖 Python 标准库，负责在 lab veth 上输出 baseline/enforce TCP 吞吐。
- 新增 Benchmark `tests/benchmark/test_network_qos_rate.sh`，验证 `network_qos` 在 2 Mbit/s TBF 目标下的实际限速误差。
- Benchmark 会关闭 lab veth 的 TSO/GSO/GRO 并使用小块 TCP 发送，避免 veth/GSO 聚合导致 TBF 误差失真。
- 121 最新 QoS rate Benchmark 目录：`results/network_policy/qos-rate-20260620-181708/`。
  - baseline：`1607.461 Mbit/s`
  - enforce：`1.976 Mbit/s`
  - 目标：`2.000 Mbit/s`
  - 误差：`-1.22%`
  - 降幅：`813.65x`
- 122 最新 QoS rate Benchmark 目录：`results/network_policy/qos-rate-20260620-181755/`。
  - baseline：`1796.228 Mbit/s`
  - enforce：`1.971 Mbit/s`
  - 目标：`2.000 Mbit/s`
  - 误差：`-1.45%`
  - 降幅：`911.33x`

### XDP 证据

- 正式子能力 `network_xdp` 默认 disabled。
- `bpf/network_xdp_demo.bpf.c` 已从单规则扩展为最多 8 条 XDP 规则，提供 generic XDP filter 和 `pass/drop/byte` 聚合统计。
- 新增构建目标 `make network-xdp-demo`。
- 新增清理脚本 `scripts/cleanup_network_xdp_demo.sh`，负责删除 lab veth/netns 并尝试卸载 XDP。
- 新增集成测试 `tests/integration/test_network_xdp.sh`，已验证：
  - `network_xdp` 能被 `--list-skills` 枚举。
  - 专用 lab netns/veth 基线连通。
  - audit 模式不挂 XDP。
  - enforce 模式在 `ep-veth-xdp0` 上挂 generic XDP 并 drop ICMP。
  - enforce 模式同时验证 TCP:19092 规则命中。
  - rollback 事件记录多规则 `drop_count >= 2`。
  - Agent 退出后无 XDP attachment 残留，连通性恢复。
- 121 最新 XDP 多规则集成测试证据目录：`results/network_policy/xdp-20260620-183031/`。
- 122 最新 XDP 多规则集成测试证据目录：`results/network_policy/xdp-20260620-184212/`。
- 121 最新完整质量门禁已通过：`reports/final_quality_gate_20260621_security_cgroup_scope.log`，17/17 P0、100 轮 Agent smoke 和 5 轮 doctor 均通过。

### TargetResolver / Pod veth 预备证据

- `resolve_netdev_target` 已增加 ifname 安全校验和 ifindex 解析，非法 ifname 返回 `invalid-ifname`，不存在的设备返回 `netdev-not-found`。
- `resolve_k8s_pod_target` 已提供默认安全的诊断型入口，默认只接受 `eulerpilot-lab` namespace；当前不执行 `kubectl` 查询、不进入 netns、不创建/删除 veth、不修改 TC/XDP。
- `docs/network_pod_veth_target.md` 已记录 reason code、安全边界和后续接入要求。
- `tests/integration/test_target_resolver.sh` 已在 121 通过，覆盖 netdev 成功/失败路径和 `k8s_pod` 的 `unsupported-namespace`、`missing-kubectl`、`missing-runtime` 诊断路径。

## 阶段 C 当前证据

- 正式 `security_policy` 已注册，`security_policy_demo` 保留为兼容回归入口。
- `configs/skills.yaml` 已新增 `security_policy`，默认 `enabled: false`、`mode: audit`，使用 schema v2 `targets + rules + target_ref` 描述 demo path target。
- `docs/security_policy_skill.md` 已补 SecurityPolicySkill 正式化说明，明确 audit/enforce、BPF LSM 安全边界、target 过滤、事件输出、回滚清理和参考代码复用边界。
- `agent/skills/security_policy/README.md` 已说明当前正式入口最小能力和后续未完成项：容器 target 绑定、规则级事件标识和进程过滤。
- `demo/security_policy_demo/README.md` 已补最小 BPF LSM demo 的运行手册和风险边界，覆盖 YAML 下发的 demo secret 文件、demo deny_exec 脚本和最多 8 项 target_map。
- `tests/integration/test_security_policy.sh` 已在 121/122 真实验证 `make agent security-policy-demo`、BPF LSM attach、四类 syscall tracepoint 事件、目标文件拒绝、demo 执行拒绝、双动态 `/tmp` YAML target_map、Agent 退出恢复和 cleanup 无残留。
- 121 最新 Security demo 集成测试证据目录：`results/security_policy/integration-20260621-095537/`。
- 122 最新 Security demo 集成测试证据目录：`results/security_policy/integration-20260621-100937/`。
- 121 最新正式 `security_policy` audit/enforce 集成测试证据目录：`results/security_policy/integration-20260621-101929/`。
- 122 最新正式 `security_policy` audit/enforce 集成测试证据目录：`results/security_policy/integration-20260621-103431/`。
- 121 最新正式 `security_policy` BPF ringbuf hit 集成测试证据目录：`results/security_policy/integration-20260621-104254/`。
- 122 最新正式 `security_policy` BPF ringbuf hit 集成测试证据目录：`results/security_policy/integration-20260621-105602/`。
- 121 最新正式 `security_policy` syscall tracing 集成测试证据目录：`results/security_policy/integration-20260621-110631/`。
- 122 最新正式 `security_policy` syscall tracing 集成测试证据目录：`results/security_policy/integration-20260621-113455/`。
- 121 最新正式 `security_policy` 四类 syscall tracing 集成测试证据目录：`results/security_policy/integration-20260621-150713/`。
- 122 最新正式 `security_policy` 四类 syscall tracing 集成测试证据目录：`results/security_policy/integration-20260621-151229/`。
- 121 最新正式 `security_policy` 双 LSM enforce 集成测试证据目录：`results/security_policy/integration-20260621-152838/`。
- 122 最新正式 `security_policy` 双 LSM enforce 集成测试证据目录：`results/security_policy/integration-20260621-153514/`。
- 121 最新正式 `security_policy` target_map 动态路径集成测试证据目录：`results/security_policy/integration-20260621-161943/`。
- 122 最新正式 `security_policy` target_map 动态路径集成测试证据目录：`results/security_policy/integration-20260621-162111/`。
- 121 最新正式 `security_policy` 多目标 target_map 集成测试证据目录：`results/security_policy/integration-20260621-164838/`。
- 122 最新正式 `security_policy` 多目标 target_map 集成测试证据目录：`results/security_policy/integration-20260621-165001/`。
- 121 最新正式 `security_policy` 规则级事件标识集成测试证据目录：`results/security_policy/integration-20260621-171904/`。
- 122 最新正式 `security_policy` 规则级事件标识集成测试证据目录：`results/security_policy/integration-20260621-171957/`。
- 121 最新正式 `security_policy` cgroup scope 集成测试证据目录：`results/security_policy/integration-20260621-173942/`。
- 122 最新正式 `security_policy` cgroup scope 集成测试证据目录：`results/security_policy/integration-20260621-174042/`。
- audit 模式证据：目标文件和 demo 执行脚本保持可访问，`security_policy_events.audit.jsonl` 包含 `operation=hit`、`result=observed`、`enforce=0`，并覆盖 `event_hook=lsm_file_open/lsm_bprm_check_security/sys_enter_execve/sys_enter_openat/sys_enter_connect/sys_enter_ptrace`。
- enforce 模式证据：目标文件和 demo 执行脚本被 BPF LSM 拒绝，`security_policy_events.enforce.jsonl` 包含 `operation=hit`、`result=blocked`、`enforce=1`；动态多目标 blocked 事件会携带单条 `rule_id/target_ref` 和 `target_index`；cgroup scoped 事件会携带 `cgroup_id/cgroup_path`，且 scope 外访问保持成功；Agent 退出后恢复可访问，无 BPF link/pin 残留。
- `scripts/cleanup_security_policy_demo.sh` 已修复无残留时因 `grep`/`pipefail` 返回非零的问题，cleanup 空跑现在正常返回 0。

## 阶段 A 后续随阶段接入项

这些内容不阻塞阶段 B 开始，但应在 Network/Security/Resource 正式接入时继续完善：

- `TargetResolver` 从诊断型 `k8s_pod` 扩展到 Pod sandbox/container PID、netns path、host veth ifindex，并接入 v2 YAML 的 `targets` 配置解析。
- `AuditBus` 从 Security 固定 path ringbuf 命中事件扩展到动态规则和更多 LSM hook 命中事件，Network/Resource 继续补全统一字段。
- `ActionJournal` 从 Security 生命周期动作扩展到动态 map 规则、container target 和异常恢复。
- `CapabilityDetector` 输出并入未来 `--status --json`。

## 进度维护规则

- 每完成阶段或关键子任务，必须更新本文件。
- 每新增目录，必须同时新增该目录的 `README.md`。
- 每个实验结果目录需要包含生成命令、环境、关键指标和结论边界。
- 每个正式 Skill 需要有独立设计文档、README、演示脚本和集成测试入口。
