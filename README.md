# EulerPilot

> 面向 openEuler 的自适应资源管控 Agent。状态基线：`2026-07-08`；本页于 `2026-07-17` 整理为 GitHub 首页入口。

EulerPilot 不是单一调度器 demo，而是一套围绕“观测 -> 决策 -> 执行 -> 反馈 -> 证据”的系统级 Agent 框架。项目使用 eBPF/PSI 感知 workload 和系统压力，在用户态完成分类、策略决策和 Skill 编排，并通过 `cgroup v2`、`sched_ext/scx`、TC/XDP、BPF LSM 等系统控制器完成可审计、可回滚的资源管控。

## 当前最强状态

| 项目 | 当前口径 |
|------|----------|
| 核心验证线 | `192.168.1.123:/root/EulerPilot`，openEuler 24.03 LTS SP4 |
| 历史对照线 | `192.168.1.121` 保留为 SP3 历史验证和回归对照；`192.168.1.122` 保留为 SP3/OLK 对照验证 |
| SP4 / sched_ext | SP4 发行环境已完成适配；`sched_ext/scx` 基于 SP4 官方源码自编译启用 `CONFIG_SCHED_CLASS_EXT` 的内核完成复核，不声称发行默认内核直接支持 |
| 质量门禁 | SP4 最终质量门禁通过：`22/22 P0`、`100` 轮 Agent smoke、`5` 轮 doctor |
| 最终证据 | `python3 scripts/collect_final_evidence.py --strict` 通过，覆盖 `37` 条核心证据，缺失 `0`、警告 `0` |
| 性能复核 | SP4 Redis/Nginx `RUNS=5`，Redis pressure gradient，Redis manual static vs agent dynamic |
| Kubernetes | 已在真实 k3s Pod 环境完成隔离验证，使用独立 namespace、独立 label、有限 resources，验证后无 EulerPilot 残留 |
| Web Console | `web_console/` 旁路展示控制台已落地，Evidence-first + 白名单 Demo + SSE 日志 + 单任务锁 |

## 架构总览

![EulerPilot 分层项目架构总览](docs/assets/eulerpilot_architecture_board.svg)

```text
Workloads / Pods / cgroups
  -> eBPF Observer / PSI / LSM / TC / XDP
  -> Agent Runtime / Workload Analyzer
  -> Policy Engine / Skill Manager
  -> Resource Control / Network Policy / Security Policy / ScxExecutor
  -> AuditBus / ActionJournal / Rollback
  -> Benchmark / Evidence / Web Console / Report
```

可编辑图源和流程图：

- `docs/assets/eulerpilot_architecture_detailed.drawio`
- `docs/assets/eulerpilot_architecture_detailed.mmd`
- `docs/assets/eulerpilot_closed_loop_flow.mmd`
- `docs/system_design.md`

## 功能模块

| 模块 | 已完成能力 | 主要证据入口 |
|------|------------|--------------|
| Agent Framework | Runtime、SkillRegistry、SkillManager、YAML 配置、`--list-skills`、`--doctor-skills`、status JSON | `agent/`、`configs/`、`docs/architecture.md` |
| CPU Scheduling / PSI | eBPF 调度观测、PSI Gate、cgroup v2 主路径、ScxExecutor/scx 增强路径 | `docs/final_results_summary.md`、`docs/sp4_validation_plan.md` |
| Resource Control | CPU + Memory + IO，`target_ref`，container/Pod cgroup 解析，事务写入和 rollback | `docs/resource_control_skill.md` |
| Network Policy | cgroup/connect4、TC QoS、XDP、真实 Pod host veth，安全白名单和 cleanup | `docs/network_policy_skill.md`、`docs/network_pod_veth_target.md` |
| Security Policy | BPF LSM、syscall tracing、服务联动 anomaly、credential anomaly、target scope | `docs/security_policy_skill.md` |
| Policy Engine | Security anomaly -> Resource / Network 联动，统一 `transaction_id`，失败回滚 | `docs/policy_engine_skill.md` |
| Web Console | 只读证据展示、白名单动作、SSE job 日志、单任务锁、cleanup | `web_console/README.md`、`docs/web_console_design.md` |

## 快速验证

普通构建和只读检查：

```bash
make agent
./build/eulerpilot-agent --validate-config configs/agent.yaml
./build/eulerpilot-agent --list-skills
./build/eulerpilot-agent --doctor-skills --config configs/agent.yaml
./build/eulerpilot-agent --status --json
```

最终证据完整性检查：

```bash
python3 scripts/collect_final_evidence.py --strict
```

最终质量门禁：

```bash
scripts/final_quality_gate.sh
```

Web Console：

```bash
cd web_console
npm ci
npm run lint
npm run test
npm run build
./scripts/run_console.sh
```

说明：涉及 cgroup、BPF、TC/XDP、LSM、sched_ext 或 Kubernetes 的集成测试需要在 openEuler 验证机上以 root 权限运行；本地普通环境建议先跑构建、配置校验、单元测试和 evidence strict。

## 核心结果

| 证据类型 | 结果目录 |
|----------|----------|
| SP4 Redis RUNS=5 | `results/final/redis-scx-compare-20260708-150702` |
| SP4 Nginx RUNS=5 | `results/final/nginx-scx-compare-20260708-152602` |
| SP4 Redis 压力梯度 | `results/final/redis-pressure-gradient-20260708-153811` |
| SP4 Redis 静态 vs Agent 动态 | `results/final/redis-static-vs-agent-20260708-162543` |
| SP4/K8s/Web Console 旁路验证 | `results/k8s/sp4-validation-20260708-023552` |
| Policy Engine SP4 repeat 10 | `results/policy_engine/security-network-resource-20260705-211407` |
| 最终 evidence compact | `reports/final_evidence_compact.md`、`reports/final_evidence_compact.json` |
| 最终质量门禁日志 | `results/k8s/sp4-validation-20260708-023552/final_quality_gate.log` |

## 文档导航

建议按下面顺序阅读：

1. `docs/one_page_summary.md`：一页式项目简介。
2. `docs/progress_status.md`：滚动进度状态，以 SP4/123 和 37 条 evidence 为当前口径。
3. `docs/final_evidence_index.md`：最终证据索引。
4. `docs/final_report_submission.md`：最终报告主稿。
5. `docs/architecture.md`：系统架构和模块边界。
6. `docs/system_design.md`：图源、流程图和环境口径。
7. `docs/demo_final_runbook.md`：答辩现场演示流程。
8. `docs/submission_checklist.md`：最终提交检查表。

重要设计文档：

- `docs/resource_control_skill.md`
- `docs/network_policy_skill.md`
- `docs/security_policy_skill.md`
- `docs/policy_engine_skill.md`
- `docs/web_console_design.md`
- `docs/sp4_validation_plan.md`
- `docs/sp4_k8s_validation_plan.md`

## 结论边界

EulerPilot 的价值不是声称所有 workload 永远优于默认调度器，而是证明：

- Agent 能低开销观测 workload 和系统压力。
- 策略能够在用户态解释、审计、回滚。
- Resource / Network / Security / Policy Engine 能在统一 Skill 框架下联动。
- Redis 等 latency-sensitive 混布场景收益更明确，Nginx 等场景存在 workload 相关边界。
- SP3 稳定主路径、OLK-6.6 对照线和 SP4 自编译 sched_ext 复核线共同支撑可交付、可迁移、可复现的比赛证据链。
