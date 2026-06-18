# 项目概述

## 比赛目标

面向 openEuler 24.03 LTS SP3 实现一个 workload-aware 的自适应资源管控 Agent。

项目需要覆盖以下能力：

- 使用 eBPF 观测 workload 行为
- 在用户态完成 workload 分类
- 在 `sched_ext/scx` 可用时执行 CPU 调度策略
- 在 `sched_ext` 不可用时提供 `cgroup v2` 主执行路径
- 提供 Skills 风格的扩展边界，覆盖 CPU、cgroup、benchmark、rollback、network、security 等方向
- 提供可复现的 benchmark 数据、文档和演示材料

## 当前项目状态

- 远端工作目录：`/root/EulerPilot`
- 主要语言：C / C++
- 工具链状态：`gcc/g++/clang/make/git/bpftool` 已安装
- 当前构建状态：Agent 可编译、可运行，并可读取真实 observer 指标
- eBPF observer：第一版 CO-RE 版本已实现，可输出 per-task 统计
- cgroup executor：已打通分类 -> profile -> `cpu.weight` -> `cgroup.procs` 的第一版闭环
- PSI：第一阶段用户态 `psi_reader` 已接入 Agent 主循环，后续再演进为 BPF 版 `psi_gate`
- scx scheduler：仍为预留后端，尚未落地
- Nginx 第二实验线：第一版正式实验链已打通，并已获得首轮有效结果

## 本机已核实事实

- OS：openEuler 24.03 LTS SP3
- Kernel：`6.6.0-132.0.0.111.oe2403sp3.x86_64`
- BTF：可用
- `CONFIG_SCHED_CLASS_EXT`：未在 `/proc/config.gz` 中检测到
- `/sys/kernel/sched_ext`：不存在
- `CONFIG_PSI=y`：存在，且 PSI 已通过启动参数启用
- unified `cgroup v2`：已挂载到 `/sys/fs/cgroup`

## 新进入项目的人需要先理解什么

这是一个围绕比赛交付构建的闭环系统：

```text
eBPF observer
  -> workload analyzer
  -> policy engine
  -> skill manager
  -> cgroup 执行优先，scx 后续增强
  -> benchmark 与报告
```

当前主 demo 目标：

```text
Redis + stress-ng
  -> 识别 redis-server 为延迟敏感 workload
  -> 识别 stress-ng 为后台干扰 workload
  -> 切换到 mixed/latency profile
  -> 保护 Redis 尾延迟
  -> 输出实验对比证据
```

## 当前策略证据分层

EulerPilot 当前不把单一指标当作唯一触发条件，而是采用分层证据设计：

### 第一层：场景前提

```text
latency_workload_exists
and background_workload_exists
```

只有同时存在“需要保护的前台服务”和“可能造成干扰的后台任务”，才进入干扰候选状态。

### 第二层：压力证据

当前和后续规划中的压力证据包括：

- eBPF 调度证据
  - `wakeup_count`
  - `total_wait_ns`
  - `runtime_ns`
  - `ctx_switch_count`
  - `migrate_count`
- PSI 压力证据
  - `cpu.some.avg10/avg60/avg300`
  - `memory.some/full`
  - `io.some/full`
- workload 角色证据
  - `redis-server/nginx/wrk`
  - `stress-ng/make/sysbench`

### 第三层：控制分级

策略不使用“全 AND”或“简单 OR”，而是：

```text
latency + background 同时存在
  -> 才允许进入控制判断

只要部分压力证据成立
  -> 进入轻度控制

PSI 压力和 latency wait 同时升高
  -> 进入强控制 mixed_profile
```

## 当前优先级

1. 继续收敛 Redis / Nginx / stress-ng 等目标 workload 的识别准确度，减少无关系统进程噪声。
2. 完善 `cgroup_control` 主执行路径，稳定输出 profile 切换和执行证据。
3. 补充 exporter / report hooks，把 profile 切换、`cpu.weight` 变化、分组行为落到实验报告。
4. 继续完善 Redis + stress-ng 和后续 Nginx 实验脚本，形成多轮可比结果。
5. `sched_ext/scx` 保持预留，等待老师和赛题方确认后再决定下一步。
6. 全部实现都围绕比赛交付要求推进：代码、实验数据、报告、演示材料。

## 当前推荐实验参数

基于当前 Redis `profile sweep` 的阶段结果，暂定推荐参数为：

```text
latency cpu.weight = 1000
background cpu.weight = 20
```

该组合在当前测试中对 `GET/INCR/SET` 的综合表现最好，后续主实验优先以这一组参数继续验证。

## 当前 PSI 接入状态

说明：本节主要记录早期阶段对 PSI 阈值与触发参数的设计依据，当前正式 compare 结果应以：

- `/root/EulerPilot/docs/final_results_summary.md`
- `/root/EulerPilot/docs/final_report_v1.md`

为准。

- 第一阶段已采用用户态 `psi_reader` 接入 `/proc/pressure/cpu|memory|io`
- PSI 已进入 Agent 主循环和实验输出
- 当前阶段仍采用静态阈值
- 在项目早期的小规模 Redis + stress-ng 负载下，`cpu_psi_high` 并不总是容易被打亮；当前正式阶段已经通过 `PsiGate v1` 与多轮 compare 继续验证该方向

## 当前推荐触发参数

当前阶段的主实验候选触发参数暂定为：

```text
cpu_psi_threshold = 0.05
latency_wait_threshold_ns = 500000
background_runtime_threshold_ns = 2500000
```

该组合来自阶段性 trigger sweep 的筛选结果；当前更正式的 Redis / Nginx compare 结果应以最新候选结果目录和总结文档为准。

## 后续验证路线

根据老师回复：

- openEuler 会在 `2026-06-30` 发布的 `openEuler 24.03-LTS-SP4` 中包含 `sched_ext` 特性
- 在此之前，可先使用 `OLK-6.6` 分支进行 `sched_ext/scx` 验证
- 技术报告中最终以 `openEuler 24.03-LTS-SP4` 作为正式目标版本

因此当前项目采用双阶段路线：

### 第一阶段：稳定主线

在当前 openEuler 24.03 LTS SP3 官方环境上，优先完成：

- eBPF 观测
- 用户态 Agent 分类与策略决策
- `cgroup v2` 主执行路径
- Redis / 后续 Nginx 实验
- 中文报告与比赛材料

### 第二阶段：并行验证 `sched_ext/scx`

后续单独准备新的 openEuler 验证环境：

1. 先基于 `OLK-6.6` 分支验证 `sched_ext/scx`
2. 待 `openEuler 24.03-LTS-SP4` 发布后，再迁移到 SP4 做正式验证
3. 在此基础上继续实现和验证 `ScxExecutor`

截至 `2026-06-10`，该路线已有阶段结果：

- 独立验证机已成功编译、安装并启动 `6.6.0-olk66-scx`
- 已确认 `CONFIG_SCHED_CLASS_EXT=y`
- 已确认 `/sys/kernel/sched_ext` 存在
- 这意味着 `sched_ext` 基础环境已具备，后续重点转为最小 `scx` 示例和执行器对接

这意味着当前不会让 `scx` 反客为主拖慢主交付，但也不会放弃后续调度增强验证。

## 参考资料

- `third_party/reference/libbpf-bootstrap`
- `third_party/reference/lmp`
- `docs/architecture.md`
- `docs/related_work.md`
- `docs/experiments.md`
- `docs/reference_repos.md`
