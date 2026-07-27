# EulerPilot 答辩讲稿

更新时间：`2026-07-26`

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
- Executor：CgroupExecutor（发行内核稳定路径）+ ScxExecutor（SP4 官方源码自编译 sched_ext 内核增强复核路径）

## 创新点（60秒）

第一，双后端统一 Agent 架构。不是两套割裂实现，同一套 Observer/Analyzer/Policy Engine 同时服务于 cgroup v2 和 sched_ext。

第二，PsiGate v1 门控状态机。NORMAL -> ARMED -> ACTIVE -> COOLDOWN，把 PSI 放在压力窗口判断里，而不是把 PSI 当成业务退化标签。

第三，Skills 插件化框架。Resource、Network、Security、Policy Engine 等能力通过 YAML 驱动，`--list-skills` 和 `--doctor-skills` 可验证，新增一个 Skill 不改动核心 Runtime。

第四，正式 compare 实验框架。支持多后端矩阵、多轮运行、平衡轮换、run_manifest、invalid_run 和中文报告。

## OS Agent 三方向覆盖（20秒）

赛题要求 eBPF hook 扩展三个方向，我们全部覆盖：

- resource control：CPU、Memory、IO 自动闭环，并支持真实 container / Kubernetes Pod target
- network policy：connect4、TC QoS、XDP、多字段 tuple 和真实 Pod host veth
- security policy：BPF LSM、syscall tracing、服务联动 anomaly 与 credential anomaly

同时，我们补了 Policy Engine 跨 Skill 联动：安全异常可以触发 Resource Control 降级，也可以同时触发 Network QoS 限速和 Resource Control 降级，所有动作都有 transaction_id、审计和 rollback。

## 实验环境（15秒）

三台环境分工：123 是 SP4 主验证和性能实验仓库，121 是比赛要求的 SP3 强制兼容交付环境，122 只保留 OLK-6.6 sched_ext 历史对照。123 的 sched_ext/scx 验证使用基于 SP4 官方源码自编译启用 `CONFIG_SCHED_CLASS_EXT` 的内核，不声称 SP4 发行默认内核直接支持 sched_ext。代码已推送 GitHub。

## Redis 结果（30秒）

历史候选：redis-scx-compare-20260612-191543，RUNS=5，平衡轮换，无 invalid_run。SP4 上追加了 redis-scx-compare-20260724-tested-2541464-runs10，用于适配与自编译 sched_ext 内核复核。v6 复审后，这批 SP4 结果被标为 provisional historical：它们说明 Redis 场景存在正向趋势，但 baseline 和 artifact provenance 还不满足封版收益结论。正式答辩数字要等 Candidate Gate、formal artifact 和修正 baseline 后重新随机化运行。

## Nginx 结果（20秒）

历史候选：nginx-scx-compare-20260612-194018，同样 RUNS=5。SP4 上追加了 nginx-scx-compare-20260724-tested-2541464-runs10，目前也按 provisional historical 保留。Nginx 结果最重要的价值是说明第二条业务线已经跑通，同时暴露 workload 差异化策略边界；正式性能结论同样等待 formal artifact 重跑。

## 证据链（15秒）

完整证据链：latency+background 场景前提 -> PsiGate 进入 ACTIVE -> cgroup_v2/sched_ext 执行动作确实发生 -> 业务结果写入历史候选或 formal artifact 结果目录。当前 evidence compact 为 41 条、缺失 0、预期警告 8；这些警告来自旧证据降级，不代表文件缺失。

## 结论（20秒）

1. SP3 上完成主闭环，是比赛要求的强制兼容交付环境。
2. OLK-6.6 和 SP4 官方源码自编译 sched_ext 内核上完成 compare 与功能复核；正式收益数字等待 v6 formal artifact 重跑。
3. Skills 框架 + Network/Security/Resource/Policy Engine 证明 Agent 可扩展、可联动、可回滚。
4. 项目已形成 41 条 evidence compact、Web Console 和 v6 preflight 证据；最终 release gate 需要在同一 candidate SHA 和 formal artifact 上重新完成。

## 收尾

> EulerPilot 已经完成了一个面向 openEuler 的、可运行、可实验、可解释、可复现、可扩展的系统资源管控 Agent 工程闭环。
