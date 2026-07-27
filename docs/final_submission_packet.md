# EulerPilot 最终提交正文包

更新时间：`2026-06-12`

## 作品名称

EulerPilot：面向 openEuler 的自适应资源管控 Agent

说明：

- 文中提到的正式候选结果目录与图表目录已经统一回收到 SP4 主验证仓库 `192.168.1.123:/root/EulerPilot`。
- `192.168.1.121` 保留为 SP3 历史验证和回归对照仓库；`192.168.1.122` 当前主要承担 `OLK-6.6 / sched_ext` 补充验证职责。

---

## 1. 作品简介

EulerPilot 是一个面向 openEuler 的自适应资源管控 Agent。与依赖外部大模型 API 的聊天型 Agent 不同，EulerPilot 的核心定位是一个本地运行的系统自治控制程序：它能够持续观测系统运行状态，识别 workload 类型，判断系统是否处于延迟敏感任务与后台干扰任务共存的压力场景，并通过 `cgroup v2` 或 `sched_ext/scx` 后端执行可解释的资源调控。

项目围绕比赛交付目标构建，目标不是给出一个最小调度器示例，而是形成一套：

- 能运行
- 能测试
- 能演示
- 能产出可复现结果
- 能写进中文技术报告

的完整系统闭环。

---

## 2. 设计思路

EulerPilot 采用统一的系统架构：

```text
Observer
-> Analyzer
-> Policy Engine
-> Executor
-> Benchmark / Report
```

其中：

- `Observer` 负责采集调度事件和 PSI 信息
- `Analyzer` 负责识别 workload 角色并提取运行证据
- `Policy Engine` 负责进行分层触发与控制决策
- `Executor` 负责把决策映射为 `cgroup v2` 或 `sched_ext` 执行动作
- `Benchmark / Report` 负责形成正式实验目录、中文报告和图表材料

整个系统并不依赖单一指标，而是采用“场景前提 + 压力证据 + 分级控制”的逻辑。也就是说，系统不会因为一个孤立指标波动就直接进入强控制，而是先判断是否存在：

```text
latency workload
and
background workload
```

再结合：

- `cpu_psi_high`
- `latency_wait_high`
- `background_runtime_high`

来决定是否进入 `latency_profile` 或 `mixed_profile`。

---

## 3. 核心实现

### 3.1 eBPF 观测

当前观测层已实现：

- `sched_wakeup`
- `sched_switch`
- `sched_migrate_task`

并输出以下 task 级指标：

- `wakeup_count`
- `total_wait_ns`
- `runtime_ns`
- `ctx_switch_count`
- `migrate_count`

### 3.2 PsiGate v1

当前 `PsiGate v1` 已完成远端闭环验证，状态机包括：

- `NORMAL`
- `ARMED`
- `ACTIVE`
- `COOLDOWN`

它的作用不是把 PSI 当成业务退化的直接标签，而是把 PSI 与调度等待、后台运行时等证据组合成压力门控逻辑。

### 3.3 双执行后端

当前执行层已完成：

- `CgroupExecutor`
- `ScxExecutor`

其中：

- `CgroupExecutor` 服务于发行内核 cgroup v2 稳定控制路径
- `ScxExecutor` 服务于 `OLK-6.6` 与 SP4 官方源码自编译 sched_ext 内核复核路径

---

## 4. 实验环境

### 4.1 SP3 历史验证环境

```text
主机：192.168.1.121
系统：openEuler 24.03 LTS SP3
定位：SP3 历史验证与回归对照，最终主验证线已切换到 SP4
```

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
- `PSI` 与 `cgroup v2` 仍可用
- 最终候选结果和图表已经回传至 `192.168.1.121`

---

## 5. 实验设计与 v6 状态

### 5.1 Redis

Redis 正式 compare 矩阵包括：

- `quiet_default`
- `quiet_scx_normal`
- `noisy_default`
- `noisy_cgroup_v2`
- `noisy_scx_normal`
- `noisy_scx_always_active`
- `noisy_scx_psi`

历史候选结果目录：

- `/root/EulerPilot/results/final/redis-scx-compare-20260612-191543`

当前 historical/provisional 结果目录：

- `/root/EulerPilot/results/final/redis-scx-compare-20260724-tested-2541464-runs10`

当前保留价值：

- `RUNS=10` 历史复核
- 固定候选轮换
- `run_manifest.json`
- 中文报告

限制：该目录因 default baseline 与 artifact provenance 问题被 v6 降级为 provisional historical，不能作为 final positive evidence。正式 Redis 结论必须从 formal `artifact_id` 重跑。

### 5.2 Nginx

Nginx 正式 compare 矩阵与 Redis 保持一致。

历史候选结果目录：

- `/root/EulerPilot/results/final/nginx-scx-compare-20260612-194018`

当前 historical/provisional 结果目录：

- `/root/EulerPilot/results/final/nginx-scx-compare-20260724-tested-2541464-runs10`

当前保留价值：

- `RUNS=10` 历史复核
- 固定候选轮换
- `run_manifest.json`
- 中文报告

限制：该目录当前为 provisional historical，不能作为 final positive evidence。正式 Nginx 结论必须从 formal `artifact_id` 重跑。

---

## 6. 实验结果

### 6.1 Redis

当前 historical/provisional 结果表明：

- `noisy_cgroup_v2` 在部分关键操作上表现出正向趋势
- `noisy_scx_normal` 和 `noisy_scx_psi` 只能作为历史策略边界证据
- `noisy_scx_always_active` 并不稳定优于其他模式

当前建议直接引用的 Redis 图表为：

- `/root/EulerPilot/reports/final_figures/redis_sched_ext_rps.svg`
- `/root/EulerPilot/reports/final_figures/redis_sched_ext_p99.svg`
- `/root/EulerPilot/reports/final_figures/redis_quiet_overhead.svg`

### 6.2 Nginx

当前 historical/provisional 结果表明：

- `noisy_cgroup_v2` 在当前 Nginx 场景下表现更稳
- `noisy_scx_psi` 吞吐接近 `noisy_default`
- 但 `noisy_scx_psi` 与 `noisy_scx_always_active` 当前仍存在明显尾延迟代价
- `quiet_scx_normal` 呈现明显基础开销，说明 `sched_ext` 常驻成本在 Nginx 场景下不能忽略

当前建议直接引用的 Nginx 图表为：

- `/root/EulerPilot/reports/final_figures/nginx_sched_ext_rps.svg`
- `/root/EulerPilot/reports/final_figures/nginx_sched_ext_p99.svg`
- `/root/EulerPilot/reports/final_figures/nginx_quiet_overhead.svg`

### 6.3 PsiGate 证据

当前建议直接引用的门控时间线为：

- `/root/EulerPilot/reports/final_figures/psigate_timeline.svg`

该图可用于说明：

- `NORMAL -> ARMED -> ACTIVE`
- 压力门控与执行动作之间的关系

---

## 7. 结果边界

本项目当前可以明确支撑：

- “工程实现完成”
- “双后端正式 compare 已成立”
- “Redis / Nginx 两条业务线都具备多轮候选结果目录”

但不应被写成：

- “sched_ext 对所有 workload 都稳定提升”
- “所有业务线都在所有模式下稳定提升”

更稳的结论应是：

> EulerPilot 已经完成从系统实现到 compare 框架的工程收口，当前 historical/provisional 结果表明 `sched_ext` 在不同 workload 上的收益与代价具有明显场景差异。最终性能收益必须等待 Candidate Gate、formal artifact 和修正 baseline 后的正式随机化实验。

---

## 8. 当前可交付状态

当前项目已经具备：

- 统一 Agent 主体
- 双执行后端
- Redis `RUNS=10` historical/provisional 结果，历史 `RUNS=5` 候选结果保留为对照
- Nginx `RUNS=10` historical/provisional 结果，历史 `RUNS=5` 候选结果保留为对照
- 中文结果摘要
- 中文最终报告主稿
- 图表材料
- 答辩提纲

也就是说，当前目录已经同时包含：

- 代码与脚本
- 历史候选结果与 v6 evidence 状态覆盖
- 最终图表材料
- 中文提交文档

因此，当前剩余工作不是扩展新功能，而是：

- Candidate Gate、Formal Artifact Gate、修正 baseline 后正式实验
- 最终文字润色
- 图表排版
- 答辩展示页美化

---

## 9. 附：当前建议同时提交的材料

- `/root/EulerPilot/docs/final_report_v2.md`
- `/root/EulerPilot/docs/final_results_summary.md`
- `/root/EulerPilot/docs/stage_delivery_summary.md`
- `/root/EulerPilot/docs/defense_summary.md`
- `/root/EulerPilot/docs/defense_slides_outline.md`
- `/root/EulerPilot/docs/delivery_package_index.md`
- `/root/EulerPilot/results/final/redis-scx-compare-20260612-191543`
- `/root/EulerPilot/results/final/nginx-scx-compare-20260612-194018`
- `/root/EulerPilot/reports/final_figures`
