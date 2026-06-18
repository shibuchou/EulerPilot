# 架构设计

EulerPilot 采用“观测 - 决策 - 执行 - 反馈”的闭环架构。

```text
Workloads
  -> eBPF Observer
  -> Agent Runtime
  -> Workload Analyzer
  -> Policy Engine
  -> Optional PolicyAdvisor
  -> Skill Manager
  -> CgroupExecutor / ScxExecutor
  -> Benchmark / Dashboard / Report
```

## 模块边界

- `bpf/`：只负责低开销观测，不做复杂策略判断。
- `agent/`：负责状态聚合、分类、策略选择和 Skill 编排。
- `Optional PolicyAdvisor`：预留的可选扩展接口，用于后续解释、总结或离线调参建议，不进入当前热路径。
- `sched/`：负责 `sched_ext/scx` 预留执行后端。
- `agent/skills/`：封装具体执行能力，例如 CPU 调度、cgroup 控制、benchmark 和 rollback。
- `bench/`：负责可复现实验和结果产出。

## 执行后端优先级

当前阶段按双后端设计推进：

```text
Policy Engine
  -> CgroupExecutor   当前官方内核主路径
  -> ScxExecutor      sched_ext 环境确认后的增强路径
```

其中：

- `CgroupExecutor`：基于 `cgroup v2` 的 `cpu.weight`、`cpu.max`、`cpuset` 等能力实现资源控制。
  当前已稳定打通的是 `cpu.weight + cgroup.procs` 主路径。
  `cpuset` 作为增强隔离能力继续保留，但不阻塞主实验。
  当前 `cpu.weight` 的解释范围限定为同一父级 `/eulerpilot` 下的 sibling cgroup：
  `latency`、`batch`、`background`。它表示同级 cgroup 之间的相对 CPU 分配权重，而不是绝对 CPU 限额。
- `ScxExecutor`：保留 `class_map`、`latency_dsq`、`batch_dsq`、`background_dsq` 等 scx 执行设计，待后续实验内核或官方支持环境验证。
  当前已完成第一版模块化执行器和 `class_map -> scx_eulerpilot` 链路，但仍处于小规模 smoke 与参数收敛阶段。

## 第一阶段闭环

第一阶段优先完成 Redis + stress-ng 混合负载：

```text
sched_wakeup/sched_switch 指标
  -> latency_score / interference_score
  -> profile 选择
  -> cgroup v2 控制后台干扰
  -> Redis P99/P999 对比
```

当前开发节奏上，`cgroup v2` 不再只是兜底，而是第一阶段必须跑通的主执行路径。

## 当前证据采集层次

EulerPilot 当前与后续规划中的证据采集分三层：

### 第一层：基础运行时证据

- eBPF 调度观测
  - `sched_wakeup`
  - `sched_switch`
  - `sched_migrate_task`
- task 级指标
  - `wakeup_count`
  - `total_wait_ns`
  - `runtime_ns`
  - `ctx_switch_count`
  - `migrate_count`

### 第二层：压力证据

- `/proc/pressure/cpu`
- `/proc/pressure/memory`
- `/proc/pressure/io`

当前 CPU PSI 已经进入实验报告，并且 `psi_reader` 已接入 Agent 主循环。
当前阶段使用用户态 PSI reader；后续第二阶段再落地更低开销、更底层的 BPF 版 `psi_gate`。

### 第三层：执行证据

- profile 选择结果
- `cpu.weight` 变化
- `cgroup.procs` 写入
- 目标进程进入 `latency/background/batch` 组的记录

## 当前策略分层

当前推荐的控制逻辑不是简单的全 AND，也不是单纯 OR，而是分层判断：

```text
第一层：
  latency_workload_exists
  and background_workload_exists

第二层：
  cpu_psi_high
  or latency_workload_wait_high
  or background_runtime_high

第三层：
  cpu_psi_high
  and latency_workload_wait_high
  -> mixed_profile
```

其中：

- `latency_profile` 对应轻度控制，当前默认 `background_weight=50`
- `mixed_profile` 对应强控制，当前候选 `background_weight=5`
- `background_weight=5` 只作为强控制候选值，不视为唯一最终值

## 框架优势

相对单一规则或单一指标，当前框架的优势在于：

- 不把 PSI 当成唯一结论，而是把它作为压力窗口信号
- 不把 eBPF 单点指标当成唯一触发，而是与 workload 角色和执行结果结合
- 支持轻度控制与强控制分级，而不是只有“控”或“不控”
- 可直接把证据链输出到 benchmark 结果和中文报告中

## 当前已完成的最小闭环

```text
observer metrics
  -> first-stage classification
  -> cgroup group selection
  -> set profile cpu.weight
  -> write cgroup.procs
```

## 当前已知限制

- `sched_ext/scx` 在当前官方 SP3 内核上仍不能作为正式主执行后端直接使用。
- `cpuset` 虽然已经进入执行设计，但当前运行路径仍以 `cpu.weight + cgroup.procs` 为稳定方案。
- 目标 workload 的识别规则在工程上已基本可用，但最终报告中仍需如实说明其仍依赖规则收敛与场景解释。

## 后续演进路线

当前架构按“两条线并行、主线优先”的方式推进：

### 当前主线

- 继续在 openEuler 24.03 LTS SP3 官方环境上打磨 `eBPF + Agent + cgroup v2 + benchmark + 中文报告`
- 优先把主实验结果、证据链和比赛材料做稳

### 后续增强线

- 先在独立 openEuler 环境上使用 `OLK-6.6` 分支验证 `sched_ext/scx`
- 待 `openEuler 24.03-LTS-SP4` 发布后，再迁移到 SP4 做正式 `sched_ext` 验证
- 后续继续把 `ScxExecutor` 从 smoke 阶段推进到正式对照实验阶段

截至 `2026-06-10`，独立 `OLK-6.6` 验证线已经完成环境部署与启动核验：

- 新内核版本：`6.6.0-olk66-scx`
- `CONFIG_SCHED_CLASS_EXT=y`
- `/sys/kernel/sched_ext` 存在

因此当前 `scx` 的主要阻塞点已经从“内核能力不存在”转为“最终结果解释、图表材料和正式报告收口”。

这样做的目的是：

- 当前不让 `scx` 阻塞主交付
- 但后续仍能在合适内核环境中完成调度增强验证
