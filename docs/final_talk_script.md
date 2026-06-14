# EulerPilot 答辩讲稿

更新时间：`2026-06-14`

## 开场（30秒）

大家好，我们的作品是 EulerPilot——面向 openEuler 的自适应资源管控 Agent。它不是聊天型 AI，而是一个本地运行的系统自治控制程序。它通过 eBPF 观测 workload，在用户态做分类和策略决策，再通过 cgroup v2 或 sched_ext 执行资源调控。

## 问题背景（20秒）

当服务器上同时存在 Redis 这样的延迟敏感服务，和 stress-ng 这样的后台干扰任务时，系统如何做出可解释、可复现、可回滚的调控决策？这就是我们要解决的问题。

## 架构（30秒）

核心结构：Observer -> Analyzer -> Policy Engine -> Skill Manager -> Executor -> 实验结果。

- Observer：eBPF 采集 sched_wakeup/switch/migrate + PSI
- Analyzer：识别 Redis/Nginx/stress-ng 等 workload
- Policy Engine：场景前提 + 压力证据 + 分级控制的三层决策
- Skill Manager：4 个 runtime skill，YAML 驱动启停
- Executor：CgroupExecutor（SP3 主线）+ ScxExecutor（OLK-6.6 增强线）

## 创新点（60秒）

第一，双后端统一 Agent 架构。不是两套割裂实现，同一套 Observer/Analyzer/Policy Engine 同时服务于 cgroup v2 和 sched_ext。

第二，PsiGate v1 门控状态机。NORMAL -> ARMED -> ACTIVE -> COOLDOWN，把 PSI 放在压力窗口判断里，而不是把 PSI 当成业务退化标签。

第三，Skills 插件化框架。4 个 runtime skill + YAML 驱动，`--list-skills` 验证，新增一个 Skill 不改动核心 Runtime。

第四，正式 compare 实验框架。支持多后端矩阵、多轮运行、平衡轮换、run_manifest、invalid_run 和中文报告。

## OS Agent 三方向覆盖（20秒）

赛题要求 eBPF hook 扩展三个方向，我们全部覆盖：

- resource control：CgroupExecutor + ScxExecutor，进入 Redis/Nginx 主实验
- network policy：cgroup/connect4 demo，attach -> deny -> recover 闭环
- security policy：BPF LSM file_open demo，同样的可演示闭环

## 实验环境（15秒）

两台环境分工：121 是 SP3 主交付环境，122 是 OLK-6.6 sched_ext 验证环境。代码已推送 GitHub。

## Redis 结果（30秒）

最强候选：redis-scx-compare-20260612-191543，RUNS=5，平衡轮换，无 invalid_run。noisy_cgroup_v2 和 noisy_scx_normal 在 GET/INCR/SET 上呈现正向趋势。但不绝对化成"全面优于默认调度器"。

## Nginx 结果（20秒）

最强候选：nginx-scx-compare-20260612-194018，同样 RUNS=5。cgroup_v2 在 Nginx 场景下更稳。sched_ext 部分模式存在明显 P99 代价。这证明框架已成功迁移到第二业务线。

## 证据链（15秒）

完整证据链：latency+background 场景前提 -> PsiGate 进入 ACTIVE -> cgroup_v2/sched_ext 执行动作确实发生 -> 业务结果写入正式目录。

## 结论（20秒）

1. SP3 上完成主闭环，可正式交付。
2. OLK-6.6 上完成 Redis/Nginx sched_ext 正式 compare，RUNS=5。
3. Skills 框架 + network/security demo 证明 Agent 可扩展。
4. 项目已进入提交冻结阶段。

## 收尾

> EulerPilot 已经完成了一个面向 openEuler 的、可运行、可实验、可解释、可复现、可扩展的系统资源管控 Agent 工程闭环。
