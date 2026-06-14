# EulerPilot 最终报告候选稿

> 说明：本文件保留为提交候选稿版本。当前建议继续润色并作为最终提交主稿使用的文档为：
>
> - `/root/EulerPilot/docs/final_report_submission.md`

更新时间：`2026-06-12`

## 摘要

EulerPilot 是一个面向 openEuler 的自适应资源管控 Agent。项目采用 eBPF 进行低开销观测，使用用户态 Agent 完成 workload 分类、压力识别和策略决策，并通过 `cgroup v2` 与 `sched_ext/scx` 双执行后端实现资源调控。当前项目已经形成从观测、分类、执行到正式实验和中文报告输出的完整工程闭环，并在 Redis 与 Nginx 两条业务线上分别形成 `RUNS=5` 的正式候选结果目录。

说明：

- 文中提到的 `sched_ext` 候选结果目录与图表目录位于远端验证机 `192.168.1.122` 的 `/root/EulerPilot` 下。

---

## 1. 研究背景

操作系统环境中的性能问题往往不是由单一 CPU 利用率指标决定的。当系统同时存在延迟敏感服务和后台干扰 workload 时，如何在保持系统可解释、可复现和可回滚的前提下进行资源调控，是一个典型的系统创新问题。

对于本赛题而言，作品既要能够在 openEuler 环境中真实运行，又要能够形成正式实验结果，而不能只停留在理论设计或单点演示。因此，EulerPilot 从一开始就不是单一调度器样例，而是一个以“可交付”为目标的 Agent 框架。

---

## 2. 设计目标

EulerPilot 的目标包括：

1. 基于 eBPF 观测 workload 运行时行为。
2. 在用户态识别 workload 角色和压力场景。
3. 通过分层证据逻辑选择控制策略。
4. 以 `cgroup v2` 或 `sched_ext/scx` 后端执行控制。
5. 输出可复现的正式实验结果、图表和中文报告。

---

## 3. 总体架构

EulerPilot 当前采用如下结构：

```text
Observer
-> Analyzer
-> Policy Engine
-> Executor
-> Benchmark / Report
```

### 3.1 Observer

当前已经实现的调度事件包括：

- `sched_wakeup`
- `sched_switch`
- `sched_migrate_task`

当前导出的任务级指标包括：

- `wakeup_count`
- `total_wait_ns`
- `runtime_ns`
- `ctx_switch_count`
- `migrate_count`

同时，系统接入 `PSI`，作为辅助识别压力窗口的信号。

### 3.2 Analyzer

当前已覆盖的核心 workload 包括：

- `redis-server`
- `nginx`
- `stress-ng`
- `make`
- `sysbench`

并将任务角色划分为：

- `LATENCY_SENSITIVE`
- `THROUGHPUT_BATCH`
- `BACKGROUND_NOISY`
- `UNKNOWN`

### 3.3 Policy Engine

当前策略层使用“场景前提 + 压力证据 + 分级控制”的逻辑：

- 场景前提：
  - `latency_exists`
  - `background_exists`
- 压力证据：
  - `cpu_psi_high`
  - `latency_wait_high`
  - `background_runtime_high`
- 控制输出：
  - `normal_profile`
  - `latency_profile`
  - `mixed_profile`
  - `throughput_profile`

为了减少 profile 来回抖动，当前采用 `2 / 2 / 5` 滞回设置。

### 3.4 Executor

当前执行层包含：

- `CgroupExecutor`
- `ScxExecutor`

其中：

- `CgroupExecutor` 服务于 `SP3` 正式主线
- `ScxExecutor` 服务于 `OLK-6.6` 正式对照线

---

## 4. 核心实现亮点

### 4.1 双后端统一 Agent 架构

项目没有把 `cgroup v2` 与 `sched_ext` 写成两套割裂实现，而是复用：

- 同一套 `Observer`
- 同一套 `Analyzer`
- 同一套 `Policy Engine`

仅在执行层切换后端。这样既能保证 `SP3` 的可交付性，又能提前验证 `sched_ext`。

### 4.2 PsiGate v1

当前 `PsiGate v1` 已完成远端闭环验证，状态包括：

- `NORMAL`
- `ARMED`
- `ACTIVE`
- `COOLDOWN`

它的设计目标不是把 PSI 作为业务退化的直接证据，而是利用 PSI 与调度等待、后台运行时等证据共同构造门控逻辑。

### 4.3 正式 compare 实验框架

当前 Redis / Nginx 的正式 compare 脚本已经支持：

- 多后端矩阵
- 多轮执行
- 平衡轮换顺序
- `run_manifest.json`
- `invalid_run`
- 中文报告

这意味着项目已经从“能跑 smoke”进入“能做正式对照”阶段。

---

## 5. 实验环境

### 5.1 主交付环境

```text
192.168.1.121
openEuler 24.03 LTS SP3
```

### 5.2 sched_ext 正式对照环境

```text
192.168.1.122
openEuler 24.03 LTS SP3
6.6.0-olk66-scx
```

该环境已经确认：

- `CONFIG_SCHED_CLASS_EXT=y`
- `/sys/kernel/sched_ext` 存在
- `PSI`、`cgroup v2`、`bpftool` 可用

---

## 6. Redis 正式结果

当前建议最终正文引用目录：

- `/root/EulerPilot/results/final/redis-scx-compare-20260612-191543`

该目录当前满足：

- `RUNS=5`
- 平衡轮换
- 无 `invalid_run`
- 完整 `run_manifest.json`
- 中文正式报告

### 当前观察

- `noisy_cgroup_v2` 在 `GET` 上表现出较明显吞吐提升趋势
- `noisy_scx_normal` 在 `GET / INCR / SET` 上表现出明显的吞吐正向趋势
- `noisy_scx_psi` 在部分操作上存在一定正向效果
- `noisy_scx_always_active` 并不稳定优于其他模式

### 当前建议配图

- `/root/EulerPilot/reports/final_figures/redis_sched_ext_rps.svg`
- `/root/EulerPilot/reports/final_figures/redis_sched_ext_p99.svg`
- `/root/EulerPilot/reports/final_figures/redis_quiet_overhead.svg`

---

## 7. Nginx 正式结果

当前建议最终正文引用目录：

- `/root/EulerPilot/results/final/nginx-scx-compare-20260612-194018`

该目录当前满足：

- `RUNS=5`
- 平衡轮换
- 无 `invalid_run`
- 完整 `run_manifest.json`
- 中文正式报告

### 当前观察

- `noisy_cgroup_v2` 在当前 Nginx 场景下表现更稳
- `noisy_scx_psi` 的吞吐接近 `noisy_default`
- 但 `noisy_scx_psi` 和 `noisy_scx_always_active` 当前仍存在明显尾延迟代价
- `quiet_scx_normal` 的基础开销不可忽略

### 当前建议配图

- `/root/EulerPilot/reports/final_figures/nginx_sched_ext_rps.svg`
- `/root/EulerPilot/reports/final_figures/nginx_sched_ext_p99.svg`
- `/root/EulerPilot/reports/final_figures/nginx_quiet_overhead.svg`

---

## 8. 关键证据链

当前结果目录已经能够给出完整证据链：

1. `latency workload + background workload` 场景成立
2. `PsiGate` 进入 `ACTIVE`
3. `cgroup_v2` 组出现 `applied=yes reason=assigned`
4. `sched_ext` 组出现 `executor=sched_ext`
5. Redis / Nginx 的业务结果写入正式候选目录

当前可直接引用的门控时间线图：

- `/root/EulerPilot/reports/final_figures/psigate_timeline.svg`

---

## 9. 结论边界

当前结果足以说明：

- 工程实现已经完成
- 正式 compare 已经成立
- 双业务线都已经有多轮候选结果

但最终报告中应避免写成：

- “sched_ext 已全面优于默认调度器”
- “所有模式都稳定带来收益”

更稳的结论应是：

> EulerPilot 已经完成从系统实现到正式 compare 的工程收口。当前结果表明，不同 `sched_ext` 模式在不同 workload 上的收益与代价具有明显场景差异，因此项目的价值主要体现在统一 Agent 架构、双后端正式实验能力和可复现证据链，而不是单一绝对化的性能优势。

---

## 10. 当前结论

当前已经可以明确得出三条结论：

1. EulerPilot 已在 `SP3` 上完成主闭环，可正式交付。
2. EulerPilot 已在 `OLK-6.6` 上完成 Redis 与 Nginx 的 `sched_ext` 正式 compare，并分别形成 `RUNS=5` 候选结果目录。
3. 当前剩余工作已从功能开发收敛为最终报告、图表与答辩材料的整理和润色。
