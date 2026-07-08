# EulerPilot 最终报告骨架

更新时间：`2026-06-12`

## 1. 作品概述

本项目面向 openEuler 设计并实现一个自适应资源管控 Agent。

核心定位不是大模型聊天 Agent，而是：

> 一个基于 eBPF 观测、规则决策和本地执行后端的系统资源调控 Agent。

最终目标是：

- 感知 workload
- 判断压力状态
- 选择控制策略
- 通过 `cgroup v2` 或 `sched_ext/scx` 执行
- 输出可复现实验结果

---

## 2. 设计目标

最终报告建议把目标拆成三层：

### 2.1 工程目标

- 在 openEuler 上可编译、可运行、可测试
- 具备双后端能力
- 具备中文实验脚本和中文结果输出

### 2.2 系统目标

- 支持观测 workload 运行时行为
- 支持压力证据分层决策
- 支持本地自治资源控制

### 2.3 比赛目标

- 有正式实验数据
- 有可复现环境
- 有中文技术报告
- 有可展示的创新点

---

## 3. 总体架构

建议用一张总图对应下列文字：

```text
Observer
-> Analyzer
-> Policy Engine
-> Executor
-> Benchmark / Report
```

文字可按下面结构展开：

### 3.1 Observer

- eBPF 调度事件
- PSI

### 3.2 Analyzer

- workload 角色识别
- latency/background 前提识别
- wait/runtime/psi 证据抽取

### 3.3 Policy Engine

- `normal`
- `latency`
- `mixed`
- `throughput`

### 3.4 Executor

- `CgroupExecutor`
- `ScxExecutor`

### 3.5 Benchmark / Report

- Redis
- Nginx
- 中文汇总
- 图表

---

## 4. 核心创新点

当前建议最终报告中突出四个点：

### 4.1 双后端统一 Agent 架构

同一套 `Observer / Analyzer / Policy Engine` 同时服务：

- `SP3 + cgroup v2`
- `OLK-6.6 + sched_ext`

### 4.2 PsiGate 分层触发

不是简单用 PSI 单独触发，也不是所有证据都做严格 AND，而是：

- `latency + background` 作为前提
- PSI / latency wait / background runtime 作为分层压力证据
- `PsiGate v1` 提供状态机门控

### 4.3 正式 compare 实验框架

当前已经不是“单轮 demo”，而是：

- 平衡轮换
- `run_manifest`
- `invalid_run`
- 中文自动报告

### 4.4 面向 openEuler 的迁移路线

- `SP3` 回答可交付性
- `OLK-6.6` 回答 sched_ext 正式对照能力
- `SP4` 回答最终迁移目标

---

## 5. 关键实现

### 5.1 eBPF 观测

写清楚：

- `sched_wakeup`
- `sched_switch`
- `sched_migrate_task`

以及当前导出的 task 级指标。

### 5.2 workload 分类

写清楚：

- `redis-server`
- `nginx`
- `stress-ng`
- `make`
- `sysbench`

### 5.3 分层触发逻辑

建议报告中用伪代码：

```text
if latency_exists and background_exists:
    if cpu_psi_high and latency_wait_high:
        -> mixed_profile
    elif cpu_psi_high or latency_wait_high or background_runtime_high:
        -> latency_profile
    else:
        -> normal_profile
```

### 5.4 CgroupExecutor

明确：

- sibling cgroup 作用域
- `cpu.weight`
- `cgroup.procs`
- `redis-benchmark` 不参与分类控制

### 5.5 ScxExecutor

明确：

- `class_map`
- `gate_state_map`
- `stats`
- `sched_ext` 生命周期

### 5.6 PsiGate v1

明确：

- `NORMAL`
- `ARMED`
- `ACTIVE`
- `COOLDOWN`

---

## 6. 实验环境

### 6.1 SP3 主环境

```text
192.168.1.121
openEuler 24.03 LTS SP3
```

### 6.2 OLK-6.6 验证环境

```text
192.168.1.122
openEuler 24.03 LTS SP3
6.6.0-olk66-scx
```

### 6.3 环境分工

- `SP3`：主交付
- `OLK-6.6`：sched_ext 正式 compare

---

## 7. 实验设计

### 7.1 Redis

当前候选目录：

- `/root/EulerPilot/results/final/redis-scx-compare-20260612-191543`

当前后端矩阵：

- `quiet_default`
- `quiet_scx_normal`
- `noisy_default`
- `noisy_cgroup_v2`
- `noisy_scx_normal`
- `noisy_scx_always_active`
- `noisy_scx_psi`

### 7.2 Nginx

当前候选目录：

- `/root/EulerPilot/results/final/nginx-scx-compare-20260612-194018`

当前后端矩阵：

- `quiet_default`
- `quiet_scx_normal`
- `noisy_default`
- `noisy_cgroup_v2`
- `noisy_scx_normal`
- `noisy_scx_always_active`
- `noisy_scx_psi`

---

## 8. 结果展示

这一节建议最终至少放下列材料：

### 8.1 Redis 后端对照表

数据来源：

- `redis-scx-compare-20260612-191543/compare_summary_avg.csv`

### 8.2 Nginx 后端对照表

数据来源：

- `nginx-scx-compare-20260612-194018/compare_summary_avg.csv`

### 8.3 关键动作证据

建议摘录：

- `noisy_cgroup_v2` 的 `applied=yes reason=assigned`
- `noisy_scx_psi` 的 `gate_state=2`
- `noisy_scx_psi` 的 `executor=sched_ext`

### 8.4 图表位

最终建议至少补：

- Redis RPS 对照图
- Redis P99 对照图
- Nginx Requests/sec 对照图
- Nginx P99 对照图
- `PsiGate` 状态时间线图

当前已生成的图表文件：

- `/root/EulerPilot/reports/final_figures/redis_sched_ext_rps.svg`
- `/root/EulerPilot/reports/final_figures/redis_sched_ext_p99.svg`
- `/root/EulerPilot/reports/final_figures/nginx_sched_ext_rps.svg`
- `/root/EulerPilot/reports/final_figures/nginx_sched_ext_p99.svg`

---

## 9. 结果边界与风险

最终报告里必须明确：

### 9.1 PSI 不是单独业务退化证明

应该写成：

> PSI 主要用于压力窗口判断，不能单独证明 Redis/Nginx 业务性能退化。

### 9.2 `cpu.weight` 是相对权重

明确：

> `cpu.weight` 表示同一父级 sibling cgroup 之间的相对 CPU 分配权重，而不是绝对 CPU 上限。

### 9.3 sched_ext 结果仍需谨慎解释

当前已经有正式候选结果，但不应写成对所有 workload 都稳定提升，而应写成：

- 某些指标出现正向趋势
- 某些模式仍有退化边界
- 需要结合业务类型理解

---

## 10. 最终结论

建议最终结论写成三句：

1. EulerPilot 已经在 SP3 上完成 `eBPF + Agent + cgroup v2` 主闭环，可交付。
2. EulerPilot 已经在 OLK-6.6 上完成 `sched_ext` 正式 compare，Redis / Nginx 均已形成多轮候选结果目录。
3. 项目已具备迁移到 `SP4` 的工程基础，后续重点在正式报告、图表与最终演示材料收口。
