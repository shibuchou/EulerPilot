# EulerPilot 答辩页提纲

更新时间：`2026-06-12`

## 第 1 页：作品概述

标题建议：

```text
EulerPilot：面向 openEuler 的自适应资源管控 Agent
```

本页只讲三件事：

- 面向 openEuler 的系统资源调控 Agent
- 不依赖外部大模型 API
- 通过 `eBPF + Agent + cgroup v2 / sched_ext` 完成本地自治控制

建议一句话：

> 我们做的是一个本地运行的系统自治 Agent，不是聊天型 Agent，它能够感知 workload，判断压力场景，并自动执行资源调控。

---

## 第 2 页：为什么做这个项目

本页核心要点：

- 比赛要求是 openEuler 上的 Agent 框架
- 需要同时回答“可交付”和“可创新”
- 因此采用双路线：
  - `SP3 + cgroup v2`
  - `OLK-6.6 + sched_ext`

建议一句话：

> 我们没有把项目赌在单一内核条件上，而是用 `SP3` 保证交付，用 `OLK-6.6` 提前验证 `sched_ext`，这样兼顾稳定性和创新性。

---

## 第 3 页：总体架构

建议放一张架构图，对应下面口径：

```text
Observer
-> Analyzer
-> Policy Engine
-> Executor
-> Benchmark / Report
```

讲解顺序：

1. `Observer`：eBPF 调度观测 + PSI
2. `Analyzer`：识别 Redis / Nginx / stress-ng
3. `Policy Engine`：分层证据判断
4. `Executor`：`CgroupExecutor / ScxExecutor`
5. `Benchmark / Report`：形成正式结果目录

---

## 第 4 页：核心创新点

建议突出四点：

### 4.1 双后端统一 Agent 架构

- 同一套主体
- 两个执行后端

### 4.2 PsiGate v1

- `NORMAL -> ARMED -> ACTIVE -> COOLDOWN`
- 压力门控而不是简单硬阈值

### 4.3 正式 compare 框架

- 平衡轮换
- `run_manifest`
- `invalid_run`
- 中文报告

### 4.4 面向 SP4 的迁移路线

- `SP3` 可交付
- `OLK-6.6` 可验证
- `SP4` 可迁移

---

## 第 5 页：环境与工程完成度

当前建议直接列两台环境：

### 5.1 SP3 主交付环境

- `192.168.1.121`
- `openEuler 24.03 LTS SP3`
- 主闭环与主交付

### 5.2 sched_ext 正式对照环境

- `192.168.1.122`
- `openEuler 24.03 LTS SP3`
- `6.6.0-olk66-scx`

建议一句话：

> 当前我们不是只有理论设计，而是已经在两台真实环境中分别把主闭环和 `sched_ext` 正式 compare 跑通了。

---

## 第 6 页：Redis 正式结果

当前建议直接引用：

- `/root/EulerPilot/results/final/redis-scx-compare-20260612-191543`

本页建议放：

- `redis_sched_ext_rps.svg`
- `redis_sched_ext_p99.svg`
- `redis_quiet_overhead.svg`

建议口径：

- `noisy_cgroup_v2` 与 `noisy_scx_normal` 在部分操作上出现正向趋势
- `noisy_scx_psi` 在 `GET` 上也呈现一定正向趋势
- 不能写成“全面优于默认调度器”

---

## 第 7 页：Nginx 正式结果

当前建议直接引用：

- `/root/EulerPilot/results/final/nginx-scx-compare-20260612-194018`

本页建议放：

- `nginx_sched_ext_rps.svg`
- `nginx_sched_ext_p99.svg`
- `nginx_quiet_overhead.svg`

建议口径：

- `cgroup_v2` 在 Nginx 场景下表现更稳
- `sched_ext` 已形成第二业务线正式 compare
- 某些 `sched_ext` 模式存在明显 P99 代价

---

## 第 8 页：关键证据链

本页建议放：

- `psigate_timeline.svg`
- `applied=yes reason=assigned` 摘录
- `executor=sched_ext` 摘录

建议按下面顺序讲：

1. 有 latency/background 场景前提
2. `PsiGate` 进入 `ACTIVE`
3. `cgroup_v2` 或 `sched_ext` 执行动作确实发生
4. 最终业务结果被记录到正式目录

---

## 第 9 页：结论边界

本页必须讲清三件事：

1. PSI 不是单独业务退化证明
2. `cpu.weight` 是相对权重，不是绝对限额
3. `sched_ext` 结果需要按 workload 谨慎解释

建议一句话：

> 我们强调的是“正式 compare 能力和系统创新闭环已经成立”，而不是把某一组结果绝对化成“所有场景都最优”。

---

## 第 10 页：最终结论

建议最终结论用三句话：

1. EulerPilot 已在 `SP3` 上完成主闭环，可交付。
2. EulerPilot 已在 `OLK-6.6` 上完成 Redis / Nginx 的 `sched_ext` 正式 compare，并形成 `RUNS=5` 候选结果目录。
3. 当前剩余工作已经从系统开发收敛为最终报告与答辩材料润色。
