# EulerPilot 答辩讲稿草案

更新时间：`2026-06-12`

## 开场

大家好，我们的作品叫做 EulerPilot，是一个面向 openEuler 的自适应资源管控 Agent。

这里的 Agent 不是聊天型 AI，而是一个本地运行的系统自治控制程序。它能够通过 eBPF 观测 workload 行为，在用户态识别延迟敏感任务和后台干扰任务，再通过 `cgroup v2` 或 `sched_ext` 后端执行资源调控，并输出正式实验结果。

## 问题背景

我们关注的问题是：在服务器环境中，如果前台存在延迟敏感服务，比如 Redis 或 Nginx，后台同时存在 stress-ng 这类干扰任务，系统如何做出可解释、可复现、可回滚的调控决策。

所以我们的目标不是只写一个调度器 demo，而是形成完整闭环：

```text
观测
-> 分析
-> 决策
-> 执行
-> 实验验证
```

## 架构

EulerPilot 的核心结构是：

```text
Observer
-> Analyzer
-> Policy Engine
-> Executor
-> Benchmark / Report
```

其中：

- `Observer` 通过 eBPF 采集调度行为
- `Analyzer` 识别 Redis、Nginx、stress-ng 等 workload
- `Policy Engine` 根据 PSI、调度等待和后台运行时做分层判断
- `Executor` 则切换到 `cgroup v2` 或 `sched_ext`

## 创新点

我想强调三个点。

第一，我们做的是双后端统一 Agent 架构，而不是两套割裂实现。

第二，我们实现了 `PsiGate v1`，采用 `NORMAL -> ARMED -> ACTIVE -> COOLDOWN` 的门控状态机，把 PSI 放在压力窗口判断里，而不是把 PSI 直接当成业务退化标签。

第三，我们不仅做了 smoke，还做了正式 compare 框架，支持多轮运行、平衡轮换、`run_manifest`、`invalid_run` 和中文报告。

## 实验环境

我们用两台环境分工。

第一台是 `192.168.1.121`，官方 `SP3` 环境，负责主闭环和主交付。

第二台是 `192.168.1.122`，基于 `OLK-6.6`，负责 `sched_ext` 的正式 compare。

这样做的目的是：

- 用 `SP3` 保证可交付
- 用 `OLK-6.6` 提前验证 `sched_ext`

## Redis 结果

当前 Redis 最强候选结果目录是：

- `/root/EulerPilot/results/final/redis-scx-compare-20260612-191543`

这轮已经做到：

- `RUNS=5`
- 平衡轮换
- 无 `invalid_run`

从结果上看：

- `noisy_cgroup_v2` 在 `GET` 上有明显提升趋势
- `noisy_scx_normal` 在 `GET / INCR / SET` 上出现了较明显的正向趋势
- `noisy_scx_psi` 在部分操作上也有一定正向效果

但我们不会把这个结果绝对化成“sched_ext 全面优于默认调度器”。

## Nginx 结果

当前 Nginx 最强候选结果目录是：

- `/root/EulerPilot/results/final/nginx-scx-compare-20260612-194018`

同样已经做到：

- `RUNS=5`
- 平衡轮换
- 无 `invalid_run`

从 Nginx 结果看：

- `cgroup_v2` 在当前场景下更稳
- `sched_ext` 某些模式吞吐接近默认调度器
- 但部分模式尾延迟代价比较明显

这说明我们的框架已经成功迁移到第二业务线，但不同 workload 对调度策略的敏感性不同。

## 关键证据链

我们可以给出完整证据链：

1. 有 latency/background 场景前提
2. `PsiGate` 进入 `ACTIVE`
3. `cgroup_v2` 出现 `applied=yes reason=assigned`
4. `sched_ext` 出现 `executor=sched_ext`
5. 最终业务结果写入正式候选目录

## 当前结论

我们当前的结论是：

1. EulerPilot 已经在 `SP3` 上完成了主闭环，可正式交付。
2. EulerPilot 已经在 `OLK-6.6` 上完成了 Redis 与 Nginx 的 `sched_ext` 正式 compare，并形成了 `RUNS=5` 候选结果目录。
3. 当前项目的主要剩余工作已经不是系统实现，而是最终报告和答辩材料的润色与整理。

## 收尾

所以我们希望展示的不是某一组绝对最优参数，而是：

> EulerPilot 已经完成了一个面向 openEuler 的、可运行、可实验、可解释、可复现的系统资源管控 Agent 工程闭环。
