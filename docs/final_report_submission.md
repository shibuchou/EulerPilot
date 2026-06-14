# EulerPilot：面向 openEuler 的自适应资源管控 Agent

更新时间：`2026-06-14`

## 摘要

EulerPilot 是一个面向 openEuler 的自适应资源管控 Agent。项目通过 eBPF 进行低开销观测，在用户态完成 workload 分类、压力识别和策略决策，并通过 `cgroup v2` 与 `sched_ext/scx` 双执行后端实现资源调控。当前项目已经完成从系统观测、策略执行到正式实验和中文报告输出的完整工程闭环，并分别在 Redis 与 Nginx 两条业务线上形成 `RUNS=5` 的候选结果目录。

说明：

- 文中提到的 `sched_ext` 候选结果目录与图表目录已经统一回收到主交付仓库 `192.168.1.121:/root/EulerPilot`。
- `192.168.1.122` 当前主要用于 `OLK-6.6 / sched_ext` 正式对照验证。

---

## 1. 项目背景

在服务器环境中，性能问题往往不是由单一 CPU 利用率指标决定的。当系统同时存在延迟敏感服务与后台干扰 workload 时，如何在保证可解释、可复现和可回滚的前提下进行资源调控，是一个典型的系统创新问题。

本赛题要求作品能够在 openEuler 环境中真实运行、形成正式实验结果、并完成完整报告。因此，EulerPilot 的设计目标不是给出一个最小调度器 demo，而是形成一套可交付的 Agent 框架：

```text
观测
-> 分析
-> 决策
-> 执行
-> 实验验证
```

---

## 2. 总体设计

EulerPilot 当前采用如下结构：

```text
Observer
-> Analyzer
-> Policy Engine
-> Executor
-> Benchmark / Report
```

### 2.1 Observer

当前已实现：

- `sched_wakeup`
- `sched_switch`
- `sched_migrate_task`

当前导出的任务级指标包括：

- `wakeup_count`
- `total_wait_ns`
- `runtime_ns`
- `ctx_switch_count`
- `migrate_count`

同时，系统接入了 `/proc/pressure/cpu|memory|io`，将 PSI 作为压力窗口的辅助证据。

### 2.2 Analyzer

当前已覆盖的典型 workload 包括：

- `redis-server`
- `nginx`
- `stress-ng`
- `make`
- `sysbench`

当前默认角色划分为：

- `LATENCY_SENSITIVE`
- `THROUGHPUT_BATCH`
- `BACKGROUND_NOISY`
- `UNKNOWN`

### 2.3 Policy Engine

当前策略层基于“场景前提 + 压力证据 + 分级控制”的逻辑运行。

场景前提：

- `latency_exists`
- `background_exists`

压力证据：

- `cpu_psi_high`
- `latency_wait_high`
- `background_runtime_high`

控制输出：

- `normal_profile`
- `latency_profile`
- `mixed_profile`
- `throughput_profile`

为减少抖动，当前默认使用 `2 / 2 / 5` 的滞回设置。

### 2.4 Executor

当前执行层已完成两个后端：

- `CgroupExecutor`
- `ScxExecutor`

其中：

- `CgroupExecutor` 服务于 `SP3` 主交付线
- `ScxExecutor` 服务于 `OLK-6.6` 上的 `sched_ext` 正式对照线

---

## 3. 核心创新点

### 3.1 双后端统一 Agent 架构

EulerPilot 没有将 `cgroup v2` 与 `sched_ext` 实现为两套割裂系统，而是复用：

- 同一套 `Observer`
- 同一套 `Analyzer`
- 同一套 `Policy Engine`

仅在执行层切换为不同后端。该设计兼顾了：

- `SP3` 的稳定交付
- `OLK-6.6` 的 `sched_ext` 正式验证
- `SP4` 的后续迁移可能

### 3.2 PsiGate v1 分层门控

当前 `PsiGate v1` 已完成远端闭环验证，状态机包括：

- `NORMAL`
- `ARMED`
- `ACTIVE`
- `COOLDOWN`

其设计目的并不是把 PSI 当作业务退化标签，而是将 PSI 与调度等待、后台运行时等证据结合，用于识别需要加强控制的压力窗口。

### 3.3 正式 compare 实验框架

当前 Redis 与 Nginx 的 `sched_ext` 正式实验脚本已经支持：

- 多后端矩阵
- 多轮执行
- 平衡轮换顺序
- `run_manifest.json`
- `invalid_run`
- 中文正式报告

因此项目已经从“功能验证”进入了“正式对照实验”阶段。


### 3.4 Skills 插件化能力框架

EulerPilot 实现了一套轻量 Skills 插件化能力框架，通过 YAML 驱动、统一接口和依赖管理，支持 Agent 能力的快速构建与扩展。

核心组件：

- `Skill` 统一基类，定义 `probe / init / start / snapshot / rollback / stop` 生命周期
- `SkillRegistry` 静态工厂，支持新增 Skill 不侵入核心 Runtime
- `SkillManager` 负责 YAML 解析、依赖拓扑排序、启停编排和回滚
- `skills.yaml` 与 `agent.yaml` 联动，真正驱动 Skill 启用与配置

当前已注册的内建 Runtime Skill 包括：

| Skill | 职责 |
|-------|------|
| `resource_control` | 封装 `CgroupExecutor / ScxExecutor` 双后端执行路径 |
| `psi_gate` | 封装 `PsiGate v1` 状态机，按后端分支探测与持有 |
| `network_policy_demo` | 基于 `cgroup/connect4` 的 eBPF 网络策略演示 |

该框架已通过 `--list-skills` 和 `--doctor-skills` 命令行验证，并在 `openEuler SP3` 和 `OLK-6.6` 双环境编译运行通过。

### 3.5 Network Policy Demo — eBPF 扩展示例

为验证 Skills 框架的可扩展性和赛题对 eBPF 作为 hook 实现 network policy agent 的要求，项目实现了一个闭环的 eBPF 网络策略演示：

| 组件 | 内容 |
|------|------|
| BPF 程序 | `cgroup/connect4`，对目标端口 18080 执行 deny |
| 控制方式 | 创建 `demo-net` cgroup，受控进程移入后自动生效 |
| 生命周期 | YAML 驱动启用 -> BPF attach -> deny 验证 -> rollback detach -> 恢复 |
| 验证结果 | cgroup 内 `curl` 被拦截（000），cgroup 外正常（200），detach 后恢复 |

该演示证明：新增一个 eBPF Skill 只需补充 BPF 程序、Skill 适配器和 `skills.yaml` 配置，无需改动 Agent 核心 Runtime 流程。
### 3.6 Security Policy Demo — BPF LSM 安全策略演示

为补齐赛题对 "eBPF 作为 hook 实现 security policy agent" 的要求，项目实现了一个基于 BPF LSM 的最小安全策略演示：

| 组件 | 内容 |
|------|------|
| BPF 程序 | `lsm/file_open`，使用 `bpf_d_path` 对目标文件路径精确匹配 |
| 控制方式 | 全局生效，不依赖 cgroup，精确拦截对指定路径的 `open()` 调用 |
| 演示目标 | `/root/EulerPilot/demo/security_policy_demo/secret.txt` |
| 生命周期 | YAML 驱动启用 -> LSM attach -> deny 验证 -> rollback detach -> 恢复 |
| 验证结果 | 启动前 `cat` 成功，启动后 `Operation not permitted`，退出后恢复 |
| 安全设计 | 不 pin link 到 BPF 文件系统，LSM 程序随 Agent 进程退出自动消亡 |

该演示与 `network_policy_demo` 保持相同的 Skill 适配器模式，证明 Security 类 eBPF hook 同样可以通过 Skills 框架实现最小闭环的 attach、deny、rollback、recover。


---

## 4. 实验环境

### 4.1 SP3 主交付环境

```text
主机：192.168.1.121
系统：openEuler 24.03 LTS SP3
定位：主闭环、主交付，以及当前统一提交目录所在环境
```

### 4.2 OLK-6.6 正式对照环境

```text
主机：192.168.1.122
系统：openEuler 24.03 LTS SP3
内核：6.6.0-olk66-scx
定位：sched_ext 正式 compare
```

该环境已经确认：

- `CONFIG_SCHED_CLASS_EXT=y`
- `/sys/kernel/sched_ext` 存在
- `PSI`、`cgroup v2`、`bpftool` 可用
- 最终候选结果与图表已回传主交付仓库

---

## 5. Redis 正式实验

当前建议直接引用的 Redis 候选结果目录为：

- `/root/EulerPilot/results/final/redis-scx-compare-20260612-191543`

该目录当前满足：

- `RUNS=5`
- 平衡轮换
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

### 5.2 当前结果观察

从当前候选结果看：

- `noisy_cgroup_v2` 在 `GET` 上表现出明显的吞吐正向趋势
- `noisy_scx_normal` 在 `GET / INCR / SET` 上表现出较明显的 RPS 改善趋势
- `noisy_scx_psi` 在 `GET` 上具备一定吞吐改善趋势
- `noisy_scx_always_active` 并不稳定优于其他模式

当前可配套引用图表：

- `/root/EulerPilot/reports/final_figures/redis_sched_ext_rps.svg`
- `/root/EulerPilot/reports/final_figures/redis_sched_ext_p99.svg`
- `/root/EulerPilot/reports/final_figures/redis_quiet_overhead.svg`

---

## 6. Nginx 正式实验

当前建议直接引用的 Nginx 候选结果目录为：

- `/root/EulerPilot/results/final/nginx-scx-compare-20260612-194018`

该目录当前满足：

- `RUNS=5`
- 平衡轮换
- `run_manifest.json`
- `compare_summary_avg.csv`
- `report.md`
- `summary.md`
- 无 `invalid_run`

### 6.1 正式矩阵

矩阵与 Redis 保持一致：

- `quiet_default`
- `quiet_scx_normal`
- `noisy_default`
- `noisy_cgroup_v2`
- `noisy_scx_normal`
- `noisy_scx_always_active`
- `noisy_scx_psi`

### 6.2 当前结果观察

从当前候选结果看：

- `noisy_cgroup_v2` 在当前 Nginx 场景下表现更稳
- `noisy_scx_psi` 吞吐接近 `noisy_default`
- `noisy_scx_psi` 与 `noisy_scx_always_active` 当前仍存在显著尾延迟代价
- `quiet_scx_normal` 的基础开销不可忽略

当前可配套引用图表：

- `/root/EulerPilot/reports/final_figures/nginx_sched_ext_rps.svg`
- `/root/EulerPilot/reports/final_figures/nginx_sched_ext_p99.svg`
- `/root/EulerPilot/reports/final_figures/nginx_quiet_overhead.svg`

---

## 7. 关键证据链

当前正式结果已经可以给出完整证据链：

1. `latency workload + background workload` 场景成立
2. `PsiGate` 进入 `ACTIVE`
3. `cgroup_v2` 组存在 `applied=yes reason=assigned`
4. `sched_ext` 组存在 `executor=sched_ext`
5. Redis / Nginx 的业务结果写入正式候选目录

当前门控时间线图：

- `/root/EulerPilot/reports/final_figures/psigate_timeline.svg`

---

## 8. 结果边界

当前结果足以支撑：

- 工程实现完成
- 正式 compare 成立
- Redis / Nginx 两条业务线均已形成多轮候选结果

但最终报告中不应写成：

- “sched_ext 全面优于默认调度器”
- “所有模式都稳定带来收益”

更稳的结论应是：

> EulerPilot 已经完成从系统实现到正式 compare 的工程收口，当前结果表明，不同 `sched_ext` 模式在不同 workload 上的收益与代价具有明显场景差异，项目的价值主要体现在统一 Agent 架构、双后端正式实验能力、Skills 插件化扩展能力、以及可复现证据链。

---

## 9. 当前结论

当前已经可以明确给出三条结论：

1. EulerPilot 已在 `SP3` 上完成主闭环，具备正式交付能力。
2. EulerPilot 已在 `OLK-6.6` 上完成 Redis 与 Nginx 的 `sched_ext` 正式 compare，并分别形成 `RUNS=5` 候选结果目录。
3. EulerPilot 已实现 Skills 插件化框架，并通过 `network_policy_demo`（cgroup/connect4）和 `security_policy_demo`（BPF LSM file_open）两个独立演示，证明了 Agent 能力可扩展和 eBPF hook 在 network/security 方向的集成可行性。
4. 当前剩余工作已经从系统开发收敛为最终报告、图表与答辩材料的整理和润色。

补充说明：

- 当前建议提交时以 `192.168.1.121:/root/EulerPilot` 作为统一交付目录。

项目代码已同步推送至 GitHub 私密仓库 `shibuchou/EulerPilot`。

当前项目已覆盖 resource control、network policy、security policy 三类 OS Agent 扩展方向：其中 resource control 进入 Redis/Nginx 主实验路径，network/security policy 作为独立 eBPF hook demo 提供 attach、deny、rollback、recover 的可演示闭环。
