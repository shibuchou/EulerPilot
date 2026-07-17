# 项目概述

更新时间：`2026-07-08`

## 比赛目标

EulerPilot 面向 openEuler 24.03 LTS 系列实现 workload-aware 的自适应资源管控 Agent，覆盖比赛要求中的用户态 Agent 框架、workload 感知、sched_ext/scx 增强路径、cgroup/resource control、network policy、security policy、可复现实验和完整报告材料。

项目目标不是做一个单点 demo，而是交付一套能真实运行、可解释、可审计、可回滚、可复现的 OS Agent 框架：

```text
eBPF/PSI 观测
-> 用户态 workload 分类
-> Policy Engine 决策
-> Skills 编排
-> cgroup v2 / sched_ext / TC / XDP / LSM 执行
-> AuditBus / ActionJournal / rollback
-> Benchmark / Evidence / Web Console / Report
```

## 当前验证基线

| 环境 | 当前角色 |
|------|----------|
| `192.168.1.123:/root/EulerPilot` | SP4 核心验证和最终交付验证仓库 |
| `192.168.1.121:/root/EulerPilot` | SP3 初代主闭环历史验证和回归对照 |
| `192.168.1.122:/root/EulerPilot` | SP3/OLK 对照验证，保留 sched_ext/scx 对照能力 |

SP4 表述统一为：

```text
SP4 发行环境已完成适配验证；
sched_ext/scx 路径基于 SP4 官方源码自编译启用 CONFIG_SCHED_CLASS_EXT 的内核完成复核；
不声称 SP4 发行默认内核直接支持 sched_ext。
```

## 当前完成状态

- Agent Runtime、SkillRegistry、SkillManager、YAML 配置、CLI 和诊断命令已完成。
- eBPF workload observer 已接入调度行为采样，PSI Gate 已进入 Agent 闭环。
- cgroup v2 是稳定主执行路径，覆盖 CPU、Memory、IO、target_ref、container/Pod target、事务写入和 rollback。
- ScxExecutor/scx_eulerpilot 已在 OLK-6.6 和 SP4 官方源码自编译启用内核上完成复核。
- Network Policy 已覆盖 cgroup/connect4、TC QoS、XDP、isolated veth 和真实 Pod host veth。
- Security Policy 已覆盖 BPF LSM、syscall tracing、服务联动 anomaly、credential lifecycle anomaly、target scope 和进程过滤。
- Policy Engine 已完成多条跨 Skill 联动，能用统一 `transaction_id` 串起 security、resource_control、network_qos 和 ActionJournal。
- Web Console v1 已作为旁路展示控制台落地，不进入 Agent 热路径，不修改主干控制逻辑。
- Kubernetes 验证使用独立 namespace、独立 label、有限 resources，并完成 cleanup 复查。

## 当前证据状态

- `scripts/final_quality_gate.sh` 在 SP4 主验证线通过 `22/22 P0`、`100` 轮 Agent smoke、`5` 轮 doctor。
- `python3 scripts/collect_final_evidence.py --strict` 通过，覆盖 `37` 条核心证据，缺失 `0`、警告 `0`。
- SP4 Redis RUNS=5：`results/final/redis-scx-compare-20260708-150702`
- SP4 Nginx RUNS=5：`results/final/nginx-scx-compare-20260708-152602`
- SP4 Redis 压力梯度：`results/final/redis-pressure-gradient-20260708-153811`
- SP4 Redis 静态 vs Agent 动态：`results/final/redis-static-vs-agent-20260708-162543`
- SP4/K8s/Web Console 旁路验证：`results/k8s/sp4-validation-20260708-023552`

## 需要先理解的设计边界

1. `cgroup v2` 是稳定交付主路径；`sched_ext/scx` 是增强验证路径。
2. `sched_ext/scx` 不作为 SP3 发行默认内核依赖。
3. Network/Security/Resource 不是孤立 demo，而是通过统一 Skill 框架、AuditBus、ActionJournal 和 Policy Engine 串联。
4. 性能结论必须保持边界：Redis 等 latency-sensitive 混布场景收益更明确，Nginx 等场景受 workload 特征影响。
5. Web Console 只读取现有 CLI、脚本、事件日志和 evidence 文件，不产生新的性能结论，不作为实验数据源。

## 推荐入口

- 当前进度：`docs/progress_status.md`
- 最终证据：`docs/final_evidence_index.md`
- 系统架构：`docs/architecture.md`
- 图示说明：`docs/system_design.md`
- 最终报告：`docs/final_report_submission.md`
- 演示流程：`docs/demo_final_runbook.md`
