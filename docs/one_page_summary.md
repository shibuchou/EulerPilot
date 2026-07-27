# EulerPilot 一页式简介

更新时间：`2026-07-27`

## 项目是什么

EulerPilot 是面向 openEuler 的自适应资源管控 Agent。它用 eBPF/PSI 感知 workload 和系统压力，在用户态完成分类、策略决策和 Skill 编排，再通过 cgroup v2、sched_ext/scx、TC/XDP、BPF LSM 等系统能力执行可审计、可回滚的控制动作。

```text
观测系统状态
-> 识别 workload 类型
-> 判断压力或安全事件
-> 选择 Resource / Network / Security / Policy Engine 策略
-> 执行控制动作
-> 输出审计、rollback 和实验结果
```

项目架构图：`docs/assets/eulerpilot_architecture_board.svg`

闭环流程图：`docs/assets/eulerpilot_closed_loop_flow.svg`

## 当前完成度

- SP4 主验证和 最终交付收口仓库：`192.168.1.123:/root/EulerPilot`。
- SP3 强制兼容交付环境：`192.168.1.121`；SP3/OLK 历史对照验证：`192.168.1.122`。
- SP4 发行环境已完成适配验证；sched_ext/scx 基于 SP4 官方源码自编译启用内核完成复核。
- SP4 final gate 已通过 `29/29 P0 + 100 smoke + 5 doctor-safe`；SP3 compatibility final gate 已通过 `10/10`。
- `python3 scripts/collect_final_evidence.py` 当前覆盖 `42` 条核心证据，缺失 `0`、预期警告 `8`；警告来自旧结果降级。
- Web Console v1 已落地为旁路展示控制台。
- Kubernetes 真实 Pod 旁路验证已完成，使用独立 namespace、独立 label、有限 resources，cleanup 后无 EulerPilot 残留。

## 赛题覆盖

| 方向 | 实现 | 状态 |
|------|------|------|
| Agent Framework | Runtime、SkillRegistry、SkillManager、YAML、CLI、AuditBus、ActionJournal | 已完成 |
| CPU Scheduling / PSI | eBPF 调度观测、PSI Gate、cgroup v2 主路径、ScxExecutor/scx 增强路径 | 已完成 |
| Resource Control Agent | CPU/Memory/IO、target_ref、container/Pod cgroup、事务写入和 rollback | 已完成 |
| Network Policy Agent | cgroup/connect4、TC QoS、XDP、真实 Pod host veth | 已完成 |
| Security Policy Agent | BPF LSM、syscall tracing、anomaly、credential lifecycle、scope 过滤 | 已完成 |
| Policy Engine | Security anomaly -> Resource / Network 联动，统一 transaction 和失败回滚 | 已完成 |

## 核心证据目录

- SP4 Redis RUNS=10 historical/provisional：`results/final/redis-scx-compare-20260724-tested-2541464-runs10`
- SP4 Nginx RUNS=10 historical/provisional：`results/final/nginx-scx-compare-20260724-tested-2541464-runs10`
- SP4 Redis 压力梯度 historical/provisional：`results/final/redis-pressure-gradient-20260724-tested-2541464-runs3`
- SP4 Redis 静态 vs Agent 动态 historical/provisional：`results/final/redis-static-vs-agent-20260724-tested-2541464-runs10`
- SP4/K8s/Web Console 旁路验证：`results/k8s/sp4-validation-20260708-023552`
- Policy Engine SP4 repeat 10：`results/policy_engine/security-network-resource-20260705-211407`
- Evidence：`reports/final_evidence_compact.md`

## 推荐阅读

- `README.md`：GitHub 首页入口。
- `docs/progress_status.md`：当前滚动进度。
- `docs/final_evidence_index.md`：最终证据索引。
- `docs/final_report_submission.md`：最终报告主稿。
- `docs/demo_final_runbook.md`：现场演示流程。

## 当前结论边界

EulerPilot 不声称所有 workload 永远优于默认调度器。当前证据表明：Redis 等 latency-sensitive 混布场景收益更明确；Nginx 等 workload 存在场景边界。项目核心价值在于统一 Agent 框架能完成观测、决策、执行、审计、rollback 和证据收口。
