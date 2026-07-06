# EulerPilot 答辩页提纲

更新时间：`2026-07-06`

## 第 1 页：作品概述
- 面向 openEuler 的自适应资源管控 Agent
- 不依赖外部大模型 API，本地自治控制
- `eBPF + Agent + cgroup v2 / sched_ext`

> 我们做的是一个本地运行的系统自治 Agent，能够感知 workload，判断压力场景，自动执行资源调控。

## 第 2 页：为什么做这个项目
- 赛题要求 openEuler 上的 Agent 框架
- 多路线兼顾稳定性与创新性：`SP3 + cgroup v2` / `OLK-6.6 + sched_ext` / `SP4 + 自编译 sched_ext 内核`

> 用 SP3 保证交付，用 OLK-6.6 和 SP4 复核 sched_ext。

## 第 3 页：总体架构
`Observer -> Analyzer -> Policy Engine -> Skill Manager -> Executor -> Benchmark/Report`

1. Observer：eBPF 调度观测 + PSI
2. Analyzer：识别 Redis/Nginx/stress-ng
3. Policy Engine：分层证据判断
4. Skill Manager：Resource / Network / Security / Policy Engine 等 Skills + YAML 驱动
5. Executor：CgroupExecutor + ScxExecutor

## 第 4 页：核心创新点
### 4.1 双后端统一 Agent 架构 — 同一套主体，两个执行后端
### 4.2 PsiGate v1 — 状态机门控，不是简单硬阈值
### 4.3 Skills 插件化框架 — 多 Skill + YAML 驱动，新增不侵入 Runtime
### 4.4 正式 compare 框架 — 平衡轮换 + run_manifest + 中文报告

## 第 5 页：OS Agent 三方向覆盖
| 方向 | 实现方式 | 状态 |
|------|----------|------|
| resource control | CPU/Memory/IO + cgroup/scx + runtime/Pod target | 已完成 |
| network policy | connect4 + TC QoS + XDP + Pod host veth | 已完成 |
| security policy | BPF LSM + syscall tracing + anomaly | 已完成 |
| policy engine | Security anomaly -> Resource / Network+Resource / real Pod | 已完成 |

## 第 6 页：环境与工程完成度
- `192.168.1.121` — SP3 主交付 + 代码开发
- `192.168.1.122` — OLK-6.6 sched_ext 正式对照
- `192.168.1.123` — SP4 自编译 sched_ext 内核复核
- GitHub `shibuchou/EulerPilot` — 代码仓库

## 第 7 页：Redis 正式结果
引用：`results/final/redis-scx-compare-20260612-191543`
SP4 复核：`results/final/redis-scx-compare-20260706-115029`
图表：redis_sched_ext_rps / p99 / quiet_overhead

## 第 8 页：Nginx 正式结果
引用：`results/final/nginx-scx-compare-20260612-194018`
SP4 复核：`results/final/nginx-scx-compare-20260706-120547`
图表：nginx_sched_ext_rps / p99 / quiet_overhead

## 第 9 页：关键证据链
1. latency + background 场景前提成立
2. PsiGate 进入 ACTIVE
3. cgroup_v2 applied=yes / sched_ext executor=sched_ext
4. 业务结果写入正式候选目录
图表：psigate_timeline.svg

## 第 10 页：结论边界
- PSI 不是单独业务退化证明
- cpu.weight 是相对权重，不是绝对限额
- sched_ext 结果需按 workload 谨慎解释

## 第 11 页：最终结论
1. SP3 上完成主闭环，可正式交付
2. OLK-6.6 与 SP4 上完成 Redis/Nginx sched_ext compare / RUNS=3 复核
3. Skills 框架 + Network/Security/Resource/Policy Engine 证明 Agent 可扩展、可联动、可回滚
4. 32 条 final evidence compact + Web Console + 最终质量门禁完成争奖证据收口
