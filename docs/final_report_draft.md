# EulerPilot 最终报告草稿

> 说明：本文件保留为历史中间草稿版本，内部 RUNS=5 叙述仅代表旧阶段结果。当前最终提交主稿已经切换为：
>
> - `/root/EulerPilot/docs/final_report_submission.md`
>
> 如需快速查看当前结果摘要与交付入口，建议优先阅读：
>
> - `/root/EulerPilot/docs/final_results_summary.md`
> - `/root/EulerPilot/docs/delivery_package_index.md`

更新时间：`2026-06-12`

## 1. 作品概述

EulerPilot 是一个面向 openEuler 的自适应资源管控 Agent。

本项目的目标不是构建一个依赖外部大模型 API 的聊天型 Agent，而是实现一个本地运行的系统自治控制程序，使系统能够：

- 感知当前 workload 类型
- 识别是否存在延迟敏感任务与后台干扰任务
- 根据压力证据选择控制策略
- 通过 `cgroup v2` 或 `sched_ext/scx` 后端执行资源调度
- 输出可复现的实验数据与中文报告

项目当前围绕“可运行、可测试、可演示、可写进报告”的主线推进，已经形成一套完整的工程闭环。

---

## 2. 设计目标

本项目的设计目标包括三个层面。

### 2.1 工程目标

- 在 openEuler 环境中可编译、可运行、可测试
- 具备统一的 Agent 主体与双执行后端
- 具备多轮实验、结果目录和中文自动报告能力

### 2.2 系统目标

- 基于 eBPF 低开销观测 workload 运行特征
- 使用分层证据逻辑判断是否需要控制
- 对延迟敏感 workload 提供保护，对后台干扰 workload 进行抑制

### 2.3 比赛目标

- 在 openEuler 上形成正式可复现结果
- 展示双后端架构与 `sched_ext` 能力
- 形成中文技术报告、图表材料和答辩可展示证据链

---

## 3. 总体架构

EulerPilot 当前采用如下总体结构：

```text
Observer
-> Analyzer
-> Policy Engine
-> Executor
-> Benchmark / Report
```

### 3.1 Observer

观测层负责从系统侧采集运行时行为。当前已经实现：

- `sched_wakeup`
- `sched_switch`
- `sched_migrate_task`

当前已导出的 task 级指标包括：

- `wakeup_count`
- `total_wait_ns`
- `runtime_ns`
- `ctx_switch_count`
- `migrate_count`

同时，系统还接入了 `PSI`，用于辅助识别压力窗口。

### 3.2 Analyzer

分析层负责识别 workload 角色和当前场景。当前已覆盖的核心对象包括：

- `redis-server`
- `nginx`
- `stress-ng`
- `make`
- `sysbench`

当前默认将任务划分为：

- `LATENCY_SENSITIVE`
- `THROUGHPUT_BATCH`
- `BACKGROUND_NOISY`
- `UNKNOWN`

### 3.3 Policy Engine

策略层不依赖单一指标，而是采用分层证据判断逻辑。当前主要包括：

- `latency_exists`
- `background_exists`
- `cpu_psi_high`
- `latency_wait_high`
- `background_runtime_high`

在此基础上输出：

- `normal_profile`
- `latency_profile`
- `mixed_profile`
- `throughput_profile`

并通过 `2 / 2 / 5` 的滞回机制避免策略频繁抖动。

### 3.4 Executor

当前执行层已完成两个后端：

- `CgroupExecutor`
- `ScxExecutor`

其中：

- `CgroupExecutor` 是 `SP3` 主交付线的正式后端
- `ScxExecutor` 是 `OLK-6.6` 上的 `sched_ext` 正式对照后端

### 3.5 Benchmark / Report

当前项目已经具备：

- Redis 正式实验链
- Nginx 正式实验链
- 多轮 compare 结果目录
- 中文 Markdown 自动报告
- SVG 图表材料

---

## 4. 核心实现与创新点

### 4.1 双后端统一架构

当前项目没有把 `cgroup v2` 和 `sched_ext` 做成两套割裂系统，而是复用：

- 同一套 `Observer`
- 同一套 `Analyzer`
- 同一套 `Policy Engine`

仅在执行层切换为：

- `CgroupExecutor`
- `ScxExecutor`

这样做的优势在于：

- `SP3` 能稳定交付
- `OLK-6.6` 能提前验证 `sched_ext`
- 后续迁移到 `SP4` 时不需要重写系统主体

### 4.2 PsiGate v1 分层门控

当前 `PsiGate v1` 已完成：

- `loader-only wiring`
- `gate_mode=normal`
- `gate_mode=always-active`
- `gate_mode=psi`
- `redis_only / redis_stress / redis_recover / redis_repeat3`

其核心思路不是简单用 PSI 单独触发，也不是所有证据都做严格 AND，而是：

- `latency + background` 作为前提
- PSI / latency wait / background runtime 作为证据
- `NORMAL / ARMED / ACTIVE / COOLDOWN` 作为门控状态

### 4.3 正式 compare 实验框架

当前 Redis 与 Nginx 的 `sched_ext` 正式实验脚本已经支持：

- 多后端矩阵
- 多轮运行
- 平衡轮换顺序
- `run_manifest.json`
- `invalid_run`
- 中文报告

这使项目从“功能验证”进入了“正式对照实验”阶段。

### 4.4 面向 openEuler 的迁移路线

当前项目已经形成明确分工：

- `192.168.1.121 / SP3`
  - 回答当前官方环境的可交付性
- `192.168.1.122 / OLK-6.6`
  - 回答 `sched_ext` 的正式对照能力
- `SP4`
  - 作为最终迁移目标环境

---

## 5. 实验环境

### 5.1 官方主交付环境

```text
主机：192.168.1.121
系统：openEuler 24.03 LTS SP3
定位：SP3 历史验证环境
```

### 5.2 sched_ext 验证环境

```text
主机：192.168.1.122
hostname：cernet2.net
系统：openEuler 24.03 LTS SP3
内核：6.6.0-olk66-scx
定位：OLK-6.6 / sched_ext 正式对照验证环境
```

### 5.3 环境分工

- `SP3`
  - 主闭环、主交付、cgroup v2 正式线
- `OLK-6.6`
  - `sched_ext` 正式 compare、`ScxExecutor`、`PsiGate`

---

## 6. Redis 正式实验结果

当前 Redis 最强候选结果目录为：

- `/root/EulerPilot/results/final/redis-scx-compare-20260612-191543`

当前已完成：

- `RUNS=5`
- 平衡轮换顺序记录
- `run_manifest.json`
- `compare_summary_avg.csv`
- `report.md`
- `summary.md`
- 无 `invalid_run`

当前正式矩阵包括：

- `quiet_default`
- `quiet_scx_normal`
- `noisy_default`
- `noisy_cgroup_v2`
- `noisy_scx_normal`
- `noisy_scx_always_active`
- `noisy_scx_psi`

当前可引用的 Redis 图表包括：

- `/root/EulerPilot/reports/final_figures/redis_sched_ext_rps.svg`
- `/root/EulerPilot/reports/final_figures/redis_sched_ext_p99.svg`
- `/root/EulerPilot/reports/final_figures/redis_quiet_overhead.svg`

从 `compare_summary_avg.csv` 可见：

- `noisy_cgroup_v2` 在 `GET` 上相对 `noisy_default` 有吞吐提升趋势
- `noisy_scx_normal` 在 `GET`、`INCR`、`SET` 上出现了较明显的 RPS 正向趋势
- `noisy_scx_psi` 在 `GET` 上相对 `noisy_default` 也表现出一定正向趋势

同时，Redis 结果也说明：

- `noisy_scx_always_active` 并不总是最佳选择
- `sched_ext` 不应被叙述为对所有 workload 都稳定提升
- 应结合不同操作类型分别理解结果

---

## 7. Nginx 正式实验结果

当前 Nginx 最强候选结果目录为：

- `/root/EulerPilot/results/final/nginx-scx-compare-20260612-194018`

当前已完成：

- `RUNS=5`
- 平衡轮换顺序记录
- `run_manifest.json`
- `compare_summary_avg.csv`
- `report.md`
- `summary.md`
- 无 `invalid_run`

当前正式矩阵与 Redis 保持一致：

- `quiet_default`
- `quiet_scx_normal`
- `noisy_default`
- `noisy_cgroup_v2`
- `noisy_scx_normal`
- `noisy_scx_always_active`
- `noisy_scx_psi`

当前可引用的 Nginx 图表包括：

- `/root/EulerPilot/reports/final_figures/nginx_sched_ext_rps.svg`
- `/root/EulerPilot/reports/final_figures/nginx_sched_ext_p99.svg`
- `/root/EulerPilot/reports/final_figures/nginx_quiet_overhead.svg`

从当前 `compare_summary_avg.csv` 可见：

- `noisy_cgroup_v2` 相对 `noisy_default` 呈现轻微正向吞吐趋势
- `noisy_scx_psi` 相对 `noisy_default` 吞吐接近，但当前 P99 仍明显偏高
- `noisy_scx_always_active` 当前在 Nginx 上表现出较大的尾延迟代价

因此 Nginx 结果当前更适合支撑：

- “框架可迁移到第二业务线”
- “双后端实验链可复用”
- “`sched_ext` 模式的收益和代价都需要按 workload 解释”

而不适合写成：

- “Nginx 上 `sched_ext` 对所有 workload 都稳定提升”

---

## 8. 关键证据链

当前正式结果目录已经可以给出完整证据链：

1. 场景前提
   - 延迟敏感 workload 存在
   - 后台干扰 workload 存在
2. 压力门控
   - `PsiGate v1` 可从 `NORMAL -> ARMED -> ACTIVE`
3. 执行动作
   - `cgroup_v2` 组存在 `applied=yes reason=assigned`
   - `sched_ext` 组存在 `executor=sched_ext`
4. 统计输出
   - `gate_status`
   - `scx_stats`
   - `class_map`
5. 业务结果
   - Redis：RPS / P99 多组对照
   - Nginx：Requests/sec / P99 多组对照

当前 `PsiGate` 状态时间线图已生成：

- `/root/EulerPilot/reports/final_figures/psigate_timeline.svg`

---

## 9. 结果边界与风险

### 9.1 PSI 不是单独业务退化证明

报告中应明确：

> PSI 主要用于判断系统压力窗口，不能单独证明 Redis 或 Nginx 的业务性能退化。

### 9.2 `cpu.weight` 是相对权重

报告中应明确：

> `cpu.weight` 表示同一父级 sibling cgroup 之间的相对 CPU 分配权重，而不是绝对 CPU 上限。

### 9.3 sched_ext 结果需要谨慎解释

当前已经有多轮正式候选结果，但合理结论应是：

- 某些模式在某些 workload 上出现正向趋势
- 某些模式也存在明显尾延迟代价
- 不同 workload 对调度策略敏感性不同

---

## 10. 当前结论

当前已经可以明确得出三条结论：

1. EulerPilot 已经在 `SP3` 上完成 `eBPF + Agent + cgroup v2` 主闭环，可交付。
2. EulerPilot 已经在 `OLK-6.6` 上完成 Redis 与 Nginx 的 `sched_ext` 正式 compare，并分别形成 `RUNS=5` 候选结果目录。
3. 项目当前已经进入最终报告、图表和答辩材料的整理阶段，而不再处于核心功能开发阶段。

---

## 11. 当前剩余工作

当前剩余工作已经明显收窄，主要是：

- 把本草稿进一步润色成正式比赛报告正文
- 在报告中插入当前已生成的图表
- 补一页最终结论边界总结
- 补答辩展示所需的精简版结果页

这意味着：

> 当前项目的主风险已经不再是“功能做不出来”，而是“如何把现有结果更好地组织成最终交付材料”。
