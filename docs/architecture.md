# 架构设计

更新时间：`2026-07-24`

EulerPilot 采用“观测 - 决策 - 执行 - 反馈”的闭环架构。当前实现已经从早期 CPU 资源控制扩展为统一 OS Agent 框架，覆盖 Resource Control、Network Policy、Security Policy、Policy Engine、Web Console 和最终证据链。

```text
Workloads / Containers / Kubernetes Pods
  -> eBPF Observer / PSI / LSM / TC / XDP / sched_ext maps
  -> Agent Runtime
  -> Workload Analyzer / TargetResolver / CapabilityDetector
  -> Policy Engine
  -> Skill Manager
  -> Resource Control / Network Policy / Security Policy / ScxExecutor
  -> AuditBus / ActionJournal / rollback
  -> Benchmark / Evidence / Web Console / Report
```

## 环境路径

| 路径 | 作用 | 当前状态 |
|------|------|----------|
| SP3 + cgroup v2 | 官方稳定主路径，证明项目可运行、可测试、可回滚 | 121 历史验证和回归对照 |
| OLK-6.6 + sched_ext | scx 提前验证线，打通 `ScxExecutor`、`class_map` 和 `scx_eulerpilot` | 122 对照验证 |
| SP4 + 自编译 sched_ext 内核 | 当前核心验证和最终交付验证线 | 123 已完成适配、RUNS=10 frozen-code 复核、K8s/Web Console 验证 |

统一口径：

```text
SP4 发行环境已完成适配验证；
sched_ext/scx 基于 SP4 官方源码自编译启用 CONFIG_SCHED_CLASS_EXT 的内核完成复核；
不声称 SP4 发行默认内核直接支持 sched_ext。
```

## 模块边界

- `bpf/`：低开销观测和 hook 程序，包括 workload observer、network connect4/TC/XDP、security LSM/syscall tracing。
- `agent/`：用户态控制面，负责配置解析、状态聚合、workload 分类、target 解析、Policy Engine 和 Skill 生命周期。
- `agent/skills/`：各 Skill 的独立实现与 README，承载 Resource/Network/Security/Policy Engine 的业务边界。
- `sched/`：`sched_ext/scx` 调度器、BPF map、DSQ 分流和 `scx_eulerpilot` 构建脚本。
- `configs/`：默认配置、Skill 配置、Policy Engine 联动配置和 final evidence manifest。
- `bench/`：Redis/Nginx、压力梯度、静态调参与 Agent 动态调控等可复现实验入口。
- `tests/`：单元测试、集成测试、benchmark 验证和真实 runtime/Pod 验证。
- `web_console/`：旁路展示控制台，只读取现有证据和白名单脚本，不进入 Agent 热路径。
- `reports/`、`results/`：最终报告、质量门禁、事件日志、图表和实验结果。

## Agent 控制面

Agent Runtime 提供统一生命周期：

```text
load config
-> detect capabilities
-> start enabled skills
-> collect observer / PSI / skill events
-> build snapshots
-> make decisions
-> apply actions through allowed skills
-> audit and journal
-> stop_all rollback
```

核心公共组件：

- `CapabilityDetector`：探测 BTF、BPF LSM、XDP、TC、cgroup v2、PSI、sched_ext、Kubernetes 等能力。
- `TargetResolver`：把 cgroup、PID、container、container_id、Kubernetes Pod 解析为真实 cgroup 或 host veth。
- `AuditBus`：统一输出 skill 事件、decision、applied、restored、failed 等审计记录。
- `ActionJournal`：记录副作用动作，支撑失败回滚和 Agent stop rollback。
- `SkillManager`：统一管理 Skill 启停、doctor、status、rollback。

## 执行后端

```text
Policy Engine / Skill Manager
  -> CgroupExecutor   稳定主路径
  -> ScxExecutor      sched_ext 环境确认后的增强路径
  -> NetworkExecutor  TC / XDP / connect4
  -> SecurityExecutor BPF LSM / syscall tracing
```

- `CgroupExecutor`：支持 `cpu.weight`、`cpu.max`、`cpuset.cpus`、`memory.high/low/max`、`io.weight/io.max`，并提供旧值读取、值校验、复读验证、审计和 rollback。
- `ScxExecutor`：使用 `class_map`、`gate_state_map`、`stats` 等 pinned maps，把用户态分类结果传给 `scx_eulerpilot`，按 latency/batch/background 分流。
- `NetworkExecutor`：限制在 lab netdev 或已解析的安全 Pod host veth 上，支持 connect4、TC/TBF、XDP drop 和 cleanup。
- `SecurityExecutor`：支持 scoped LSM enforce、syscall tracing、anomaly 聚合和 credential 生命周期事件。

## Skill 能力层

| Skill | 主要能力 | 当前状态 |
|-------|----------|----------|
| `resource_control` | CPU/Memory/IO、target_ref、container/Pod cgroup、quota sweep、rollback | 已完成 |
| `network_policy` | connect4 audit/enforce、TC QoS、XDP、real Pod host veth | 已完成 |
| `security_policy` | LSM、syscall tracing、anomaly、credential lifecycle、scope 过滤 | 已完成 |
| `policy_engine` | 跨 Skill 联动、统一 transaction、多动作失败回滚 | 已完成 |
| `web_console` | Evidence-first 展示、白名单 Demo、SSE job 日志、单任务锁 | 已完成 |

## Policy Engine 联动

当前已完成三类核心链路：

```text
security_policy burst_execve
  -> policy_engine
  -> resource_control cgroup 降级
  -> rollback

security_policy burst_connect
  -> policy_engine
  -> resource_control demo_cgroup
  -> network_qos lab_netdev
  -> rollback

security_policy burst_connect
  -> policy_engine target_ref=lab_pod(type=k8s_pod)
  -> resource_control Pod cgroup
  -> network_qos Pod host veth
  -> rollback
```

这些链路都通过 `transaction_id` 串起 security、policy_engine、resource_control、network_qos 和 ActionJournal。

## 证据和交付层

当前最终证据由以下入口收口：

- `configs/final_evidence_manifest.json`
- `scripts/collect_final_evidence.py`
- `reports/final_evidence_compact.md`
- `reports/final_evidence_compact.json`
- `docs/final_evidence_index.md`
- `docs/final_report_submission.md`
- `docs/demo_final_runbook.md`

当前 strict evidence 覆盖 `40` 条核心证据，必需缺失 `0`、警告 `0`。最终质量门禁在 SP4 主验证线上通过 `22/22 P0`、`100` 轮 Agent smoke 和 `5` 轮 doctor。

## 设计边界

- 不把 SP4 发行默认内核描述为直接支持 `sched_ext`。
- 不把 Redis/Nginx 所有场景都写成无条件性能提升。
- 不让 Web Console 产生新的实验结论；它只展示现有 CLI、脚本、事件日志和 evidence 文件。
- Kubernetes 验证必须旁路、隔离、可清理，不修改生产 namespace 或既有 workload。
- `third_party/reference/` 只作为参考快照，不作为 EulerPilot 生产代码直接复制。
