# EulerPilot：面向 openEuler 的自适应资源管控 Agent

> 说明：本文件保留为 `v1` 历史阶段版本，内部 RUNS=5 叙述仅代表旧阶段结果。当前最终提交主稿已经切换为：
>
> - `/root/EulerPilot/docs/final_report_submission.md`

更新时间：`2026-06-12`

## 摘要

EulerPilot 是一个面向 openEuler 的自适应资源管控 Agent。项目以系统自治控制为核心目标，通过 eBPF 低开销观测 workload 的调度行为，结合本地规则决策、压力门控和双执行后端，实现对延迟敏感任务的保护与后台干扰任务的抑制。

当前项目已经完成两条正式交付线：

- `SP3 + cgroup v2` 主闭环交付线
- `OLK-6.6 + sched_ext` 正式对照验证线

在实验层面，项目已经形成 Redis 与 Nginx 两条业务线的多轮正式候选结果目录。其中：

- Redis `sched_ext` 正式对照已经完成 `RUNS=5`
- Nginx `sched_ext` 正式对照已经完成 `RUNS=5`

同时，项目已经生成中文结果摘要、中文报告草稿以及 SVG 图表材料，当前工作重点已经从系统功能开发转入最终报告与答辩材料整理。

---

## 1. 项目背景与目标

第三届中国研究生操作系统开源创新大赛系统创新赛道要求作品能够在 openEuler 环境中完成 workload 感知、资源控制与可复现实验。本项目围绕这一要求实现了一个本地运行的系统资源调控 Agent，而不是依赖外部大模型 API 的聊天型 Agent。

EulerPilot 的目标可以概括为：

1. 感知系统中当前运行的 workload 类型。
2. 判断系统是否处于“延迟敏感任务 + 后台干扰任务”共存的压力场景。
3. 选择适当的控制策略。
4. 通过 `cgroup v2` 或 `sched_ext/scx` 后端执行调控。
5. 输出可复现的正式实验结果与中文报告。

---

## 2. 总体架构

EulerPilot 当前采用统一架构：

```text
Observer
-> Analyzer
-> Policy Engine
-> Executor
-> Benchmark / Report
```

### 2.1 Observer

观测层负责采集低开销运行时特征。当前已实现的内核侧事件包括：

- `sched_wakeup`
- `sched_switch`
- `sched_migrate_task`

当前导出的 task 级指标包括：

- `wakeup_count`
- `total_wait_ns`
- `runtime_ns`
- `ctx_switch_count`
- `migrate_count`

此外，系统还接入了 `PSI`，作为压力窗口判断的辅助信号。

### 2.2 Analyzer

分析层负责识别 workload 角色并构造调度证据。当前已覆盖：

- `redis-server`
- `nginx`
- `stress-ng`
- `make`
- `sysbench`

当前默认角色包括：

- `LATENCY_SENSITIVE`
- `THROUGHPUT_BATCH`
- `BACKGROUND_NOISY`
- `UNKNOWN`

### 2.3 Policy Engine

策略层采用分层判断逻辑，而不是依赖单一阈值。当前主要证据包括：

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

当前默认滞回设置为：

- `enter_latency_requires = 2`
- `enter_mixed_requires = 2`
- `exit_to_normal_requires = 5`

### 2.4 Executor

执行层当前已经完成双后端：

- `CgroupExecutor`
- `ScxExecutor`

其中：

- `CgroupExecutor` 服务于 `SP3` 主交付线
- `ScxExecutor` 服务于 `OLK-6.6` 上的 `sched_ext` 正式对照线

---

## 3. 核心实现

### 3.1 CgroupExecutor

当前 `cgroup v2` 方案以同一父层级 sibling cgroup 为基本作用域：

```text
/sys/fs/cgroup/eulerpilot/
  latency/
  batch/
  background/
```

执行方式包括：

- `cpu.weight`
- `cgroup.procs`

需要明确的是：

> `cpu.weight` 表示同一父级 sibling cgroup 之间的相对 CPU 分配权重，而不是绝对 CPU 限额。

### 3.2 ScxExecutor

当前 `sched_ext` 路线已经从最小可用原型提升到正式 compare 入口。当前实现包含：

- `class_map`
- `gate_state_map`
- `stats`
- `sched_ext` 生命周期管理

当前约定：

- `0 = normal`
- `1 = latency`
- `2 = batch`
- `3 = background`

### 3.3 PsiGate v1

当前 `PsiGate v1` 已完成远端功能闭环验证，状态机包括：

- `NORMAL`
- `ARMED`
- `ACTIVE`
- `COOLDOWN`

其设计目标不是把 PSI 当作业务性能退化的直接证明，而是：

- 利用 PSI 辅助识别压力窗口
- 与延迟敏感任务等待证据、后台运行时证据共同构成门控逻辑

这使 `sched_ext` 后端能够在“何时加强控制”这一点上具备更强的可解释性。

---

## 4. 实验环境

### 4.1 官方主交付环境

```text
主机：192.168.1.121
系统：openEuler 24.03 LTS SP3
定位：SP3 历史验证环境
```

该环境当前用于：

- `eBPF + Agent + cgroup v2` 主闭环
- Redis / Nginx 主线实验
- 中文文档与结果材料生成

### 4.2 sched_ext 正式对照环境

```text
主机：192.168.1.122
hostname：cernet2.net
系统：openEuler 24.03 LTS SP3
内核：6.6.0-olk66-scx
定位：OLK-6.6 / sched_ext 正式对照验证环境
```

该环境已经确认：

- `CONFIG_SCHED_CLASS_EXT=y`
- `/sys/kernel/sched_ext` 存在
- `PSI`、`cgroup v2`、`bpftool` 仍可用

---

## 5. Redis 正式实验结果

当前 Redis 最强候选结果目录为：

- `/root/EulerPilot/results/final/redis-scx-compare-20260612-191543`

该目录当前已满足：

- `RUNS=5`
- 平衡轮换顺序记录
- `run_manifest.json`
- `compare_summary_avg.csv`
- `report.md`
- `summary.md`
- 无 `invalid_run`

### 5.1 正式矩阵

- `quiet_default`
- `quiet_scx_normal`
- `noisy_default`
- `noisy_cgroup_v2`
- `noisy_scx_normal`
- `noisy_scx_always_active`
- `noisy_scx_psi`

### 5.2 结果观察

基于 `compare_summary_avg.csv` 可见：

- `noisy_cgroup_v2`
  - 在 `GET` 上相对 `noisy_default` 呈现明显吞吐提升趋势
- `noisy_scx_normal`
  - 在 `GET`、`INCR`、`SET` 上表现出较明显的 RPS 正向趋势
  - 在 `SET` 上同时具有较明显的 P99 改善趋势
- `noisy_scx_psi`
  - 在 `GET` 上具备一定吞吐提升趋势
  - 但并不是所有操作都优于 `noisy_default`
- `noisy_scx_always_active`
  - 并不稳定优于其他模式
  - 在部分操作上存在更高的尾延迟代价

### 5.3 当前图表

- `/root/EulerPilot/reports/final_figures/redis_sched_ext_rps.svg`
- `/root/EulerPilot/reports/final_figures/redis_sched_ext_p99.svg`
- `/root/EulerPilot/reports/final_figures/redis_quiet_overhead.svg`

---

## 6. Nginx 正式实验结果

当前 Nginx 最强候选结果目录为：

- `/root/EulerPilot/results/final/nginx-scx-compare-20260612-194018`

该目录当前已满足：

- `RUNS=5`
- 平衡轮换顺序记录
- `run_manifest.json`
- `compare_summary_avg.csv`
- `report.md`
- `summary.md`
- 无 `invalid_run`

### 6.1 正式矩阵

Nginx 的正式矩阵与 Redis 保持一致：

- `quiet_default`
- `quiet_scx_normal`
- `noisy_default`
- `noisy_cgroup_v2`
- `noisy_scx_normal`
- `noisy_scx_always_active`
- `noisy_scx_psi`

### 6.2 结果观察

基于 `compare_summary_avg.csv` 可见：

- `noisy_cgroup_v2`
  - 相对 `noisy_default` 呈现轻微正向吞吐趋势
  - 当前 P99 未出现恶化
- `noisy_scx_psi`
  - 吞吐与 `noisy_default` 接近
  - 但当前 P99 仍明显偏高
- `noisy_scx_always_active`
  - 当前尾延迟代价很明显
- `quiet_scx_normal`
  - 存在明显基础开销，说明 `sched_ext` 常驻开销在 Nginx 场景下不能忽略

### 6.3 当前图表

- `/root/EulerPilot/reports/final_figures/nginx_sched_ext_rps.svg`
- `/root/EulerPilot/reports/final_figures/nginx_sched_ext_p99.svg`
- `/root/EulerPilot/reports/final_figures/nginx_quiet_overhead.svg`

---

## 7. 关键证据链

当前正式结果目录已经能够给出完整证据链：

1. 场景前提
   - 延迟敏感 workload 存在
   - 后台干扰 workload 存在
2. 门控状态
   - `PsiGate v1` 可从 `NORMAL -> ARMED -> ACTIVE`
3. 执行动作
   - `cgroup_v2` 组可见 `applied=yes reason=assigned`
   - `sched_ext` 组可见 `executor=sched_ext`
4. 统计与状态
   - `gate_status`
   - `scx_stats`
   - `class_map`
5. 业务结果
   - Redis：RPS / P99 多组对照
   - Nginx：Requests/sec / P99 多组对照

当前已生成的 `PsiGate` 时间线图为：

- `/root/EulerPilot/reports/final_figures/psigate_timeline.svg`

---

## 8. 结果边界

当前结果足以支撑“工程实现完成”和“正式 compare 已成立”这两个判断，但仍需明确结论边界：

### 8.1 PSI 不是业务退化的单独证明

应写成：

> PSI 主要用于识别系统压力窗口，不能单独证明 Redis 或 Nginx 的业务性能退化。

### 8.2 `cpu.weight` 是相对权重

应写成：

> `cpu.weight` 表示同一父级 sibling cgroup 之间的相对 CPU 分配权重，而不是绝对 CPU 上限。

### 8.3 sched_ext 结果不能简单绝对化

当前更稳的结论是：

- `sched_ext` 已形成正式 compare 框架
- 某些模式在某些 workload 上出现正向趋势
- 某些模式也存在明显尾延迟代价
- 不同 workload 对调度模式的敏感性不同

---

## 9. 当前结论

当前已经可以给出三条明确结论：

1. EulerPilot 已经在 `SP3` 上完成 `eBPF + Agent + cgroup v2` 主闭环，具备正式交付能力。
2. EulerPilot 已经在 `OLK-6.6` 上完成 Redis 与 Nginx 两条业务线的 `sched_ext` 正式 compare，并分别形成 `RUNS=5` 候选结果目录。
3. 项目当前已经从功能开发阶段进入最终交付整理阶段，剩余工作主要是正式报告与展示材料润色。

---

## 10. 当前剩余工作

当前剩余工作已经明确收敛为：

- 把本草稿润色为最终提交正文
- 将当前图表插入正式报告并统一排版
- 做一版更精简的答辩展示页
- 统一术语与结论边界表述

也就是说：

> 当前项目已经不再缺核心实现、正式实验和候选结果，剩下的是最终提交材料的组织与润色。
