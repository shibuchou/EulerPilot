# EulerPilot：面向 openEuler 的自适应资源管控 Agent

> 说明：本文件保留为 `v2` 历史阶段版本，内部 RUNS=5 叙述仅代表旧阶段结果。当前最终提交主稿已经切换为：
>
> - `/root/EulerPilot/docs/final_report_submission.md`

更新时间：`2026-06-12`

## 摘要

EulerPilot 是一个面向 openEuler 的自适应资源管控 Agent。项目通过 eBPF 低开销观测 workload 的调度行为，结合本地规则决策、PSI 门控和双执行后端，实现对延迟敏感任务的保护与后台干扰任务的抑制。与依赖外部大模型 API 的聊天型 Agent 不同，EulerPilot 的核心价值在于系统自治控制能力：能够在本地持续运行、感知环境、做出可解释决策，并将决策转换为可复现的调度动作与实验结果。

说明：

- 文中提到的 `sched_ext` 候选结果目录与图表目录位于远端验证机 `192.168.1.122` 的 `/root/EulerPilot` 下。

当前项目已经形成两条正式交付线：

- `SP3 + cgroup v2` 主闭环交付线
- `OLK-6.6 + sched_ext` 正式对照验证线

在实验层面，项目已经完成 Redis 与 Nginx 两条业务线的 `sched_ext` 正式 compare，并分别形成 `RUNS=5` 的候选结果目录：

- Redis：`/root/EulerPilot/results/final/redis-scx-compare-20260612-191543`
- Nginx：`/root/EulerPilot/results/final/nginx-scx-compare-20260612-194018`

同时，项目已经生成中文结果摘要、中文报告草稿以及 SVG 图表材料，当前工作重点已经从系统功能开发转入最终报告与答辩材料整理。

---

## 1. 项目背景

第三届中国研究生操作系统开源创新大赛系统创新赛道要求作品能够在 openEuler 环境中完成 workload 感知、资源控制与可复现实验。对于该题目，单纯给出架构设计或最小演示还不够，必须真正解决以下三个问题：

1. 如何在真实 openEuler 环境中稳定运行。
2. 如何把系统观测、策略决策和调度执行组织成统一闭环。
3. 如何给出正式、可复现、可解释的实验结果。

EulerPilot 的设计就是围绕这三个问题展开。项目不把“Agent”理解为聊天式 AI，而是将其定义为一个本地运行的系统自治控制程序：

```text
观测系统状态
-> 判断 workload 类型
-> 选择控制策略
-> 通过 cgroup 或 sched_ext 执行
-> 输出性能结果和执行证据
```

---

## 2. 设计目标

EulerPilot 的设计目标可以概括为三个层面。

### 2.1 工程目标

- 在 openEuler 环境中可编译、可运行、可测试
- 具备统一的 Agent 主体与双执行后端
- 具备多轮实验、结果目录和中文自动报告能力

### 2.2 系统目标

- 基于 eBPF 低开销观测 workload 运行特征
- 使用分层证据逻辑判断是否需要控制
- 对延迟敏感 workload 提供保护，对后台干扰 workload 进行抑制

### 2.3 交付目标

- 形成 Redis / Nginx 两条业务线的正式结果
- 形成中文技术报告与图表材料
- 形成面向 SP4 的迁移路线

---

## 3. 总体架构

EulerPilot 当前采用统一架构：

```text
Observer
-> Analyzer
-> Policy Engine
-> Executor
-> Benchmark / Report
```

### 3.1 Observer

观测层负责采集低开销运行时特征。当前已实现：

- `sched_wakeup`
- `sched_switch`
- `sched_migrate_task`

当前导出的 task 级指标包括：

- `wakeup_count`
- `total_wait_ns`
- `runtime_ns`
- `ctx_switch_count`
- `migrate_count`

同时，系统已接入 `/proc/pressure/cpu|memory|io`，将 PSI 作为压力窗口辅助证据。

### 3.2 Analyzer

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

### 3.3 Policy Engine

策略层采用分层证据判断逻辑，当前核心证据包括：

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

为避免策略频繁抖动，当前默认滞回设置为：

- `enter_latency_requires = 2`
- `enter_mixed_requires = 2`
- `exit_to_normal_requires = 5`

### 3.4 Executor

执行层当前已经完成两个后端：

- `CgroupExecutor`
- `ScxExecutor`

其中：

- `CgroupExecutor` 服务于 `SP3` 主交付线
- `ScxExecutor` 服务于 `OLK-6.6` 上的 `sched_ext` 正式对照线

---

## 4. 核心实现与创新点

### 4.1 双后端统一 Agent 架构

EulerPilot 没有把 `cgroup v2` 和 `sched_ext` 做成两套割裂系统，而是复用：

- 同一套 `Observer`
- 同一套 `Analyzer`
- 同一套 `Policy Engine`

仅在执行层切换为：

- `CgroupExecutor`
- `ScxExecutor`

这种设计的实际价值在于：

- `SP3` 环境可以保证当前交付稳定
- `OLK-6.6` 可以提前验证 `sched_ext`
- 后续迁移到 `SP4` 时不需要重写系统主体

### 4.2 PsiGate v1 分层门控

当前 `PsiGate v1` 已完成：

- `loader-only wiring`
- `gate_mode=normal`
- `gate_mode=always-active`
- `gate_mode=psi`
- `redis_only / redis_stress / redis_recover / redis_repeat3`

其核心思路不是简单用 PSI 单独触发，也不是要求所有证据都严格同时满足，而是：

- `latency + background` 作为场景前提
- PSI / latency wait / background runtime 作为压力证据
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

### 4.4 面向 SP4 的迁移路线

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

该环境当前用于：

- `eBPF + Agent + cgroup v2` 主闭环
- Redis / Nginx 主线实验
- 中文文档与结果材料生成

### 5.2 sched_ext 正式对照环境

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

## 6. Redis 正式实验结果

当前 Redis 最强候选结果目录为：

- `/root/EulerPilot/results/final/redis-scx-compare-20260612-191543`

该目录已满足：

- `RUNS=5`
- 平衡轮换顺序记录
- `run_manifest.json`
- `compare_summary_avg.csv`
- `report.md`
- `summary.md`
- 无 `invalid_run`

### 6.1 正式矩阵

- `quiet_default`
- `quiet_scx_normal`
- `noisy_default`
- `noisy_cgroup_v2`
- `noisy_scx_normal`
- `noisy_scx_always_active`
- `noisy_scx_psi`

### 6.2 结果观察

从最终候选结果看：

- `noisy_cgroup_v2`
  - 在 `GET` 上相对 `noisy_default` 呈现明显吞吐提升趋势
- `noisy_scx_normal`
  - 在 `GET`、`INCR`、`SET` 上表现出较明显的 RPS 正向趋势
  - 在 `SET` 上同时表现出较明显的 P99 改善趋势
- `noisy_scx_psi`
  - 在 `GET` 上具备一定吞吐提升趋势
  - 但并不是所有操作都优于 `noisy_default`
- `noisy_scx_always_active`
  - 并不稳定优于其他模式
  - 在部分操作上存在更高的尾延迟代价

### 6.3 可引用图表

- `/root/EulerPilot/reports/final_figures/redis_sched_ext_rps.svg`
- `/root/EulerPilot/reports/final_figures/redis_sched_ext_p99.svg`
- `/root/EulerPilot/reports/final_figures/redis_quiet_overhead.svg`

---

## 7. Nginx 正式实验结果

当前 Nginx 最强候选结果目录为：

- `/root/EulerPilot/results/final/nginx-scx-compare-20260612-194018`

该目录已满足：

- `RUNS=5`
- 平衡轮换顺序记录
- `run_manifest.json`
- `compare_summary_avg.csv`
- `report.md`
- `summary.md`
- 无 `invalid_run`

### 7.1 正式矩阵

Nginx 的正式矩阵与 Redis 保持一致：

- `quiet_default`
- `quiet_scx_normal`
- `noisy_default`
- `noisy_cgroup_v2`
- `noisy_scx_normal`
- `noisy_scx_always_active`
- `noisy_scx_psi`

### 7.2 结果观察

从当前候选结果看：

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

### 7.3 可引用图表

- `/root/EulerPilot/reports/final_figures/nginx_sched_ext_rps.svg`
- `/root/EulerPilot/reports/final_figures/nginx_sched_ext_p99.svg`
- `/root/EulerPilot/reports/final_figures/nginx_quiet_overhead.svg`

---

## 8. 关键证据链

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

## 9. 结果边界

当前结果足以支撑“工程实现完成”和“正式 compare 已成立”这两个判断，但最终报告中仍需明确结论边界。

### 9.1 PSI 不是业务退化的单独证明

应写成：

> PSI 主要用于识别系统压力窗口，不能单独证明 Redis 或 Nginx 的业务性能退化。

### 9.2 `cpu.weight` 是相对权重

应写成：

> `cpu.weight` 表示同一父级 sibling cgroup 之间的相对 CPU 分配权重，而不是绝对 CPU 上限。

### 9.3 sched_ext 结果不能简单绝对化

当前更稳的结论是：

- `sched_ext` 已形成正式 compare 框架
- 某些模式在某些 workload 上出现正向趋势
- 某些模式也存在明显尾延迟代价
- 不同 workload 对调度模式的敏感性不同

---

## 10. 当前结论

当前已经可以给出三条明确结论：

1. EulerPilot 已经在 `SP3` 上完成 `eBPF + Agent + cgroup v2` 主闭环，具备正式交付能力。
2. EulerPilot 已经在 `OLK-6.6` 上完成 Redis 与 Nginx 两条业务线的 `sched_ext` 正式 compare，并分别形成 `RUNS=5` 候选结果目录。
3. 项目当前已经从功能开发阶段进入最终交付整理阶段，剩余工作主要是正式报告与展示材料润色。

---

## 11. 当前剩余工作

当前剩余工作已经收敛为：

- 把本稿进一步润色为最终提交正文
- 将当前图表插入正式报告并统一排版
- 依据 `defense_slides_outline.md` 制作最终答辩页
- 统一术语与结论边界表述

因此当前项目的真实状态是：

> 已完成系统实现、正式实验、候选结果目录和基础展示材料，剩余工作已经主要是最终提交文稿与答辩展示整理。
