# EulerPilot：面向 openEuler 的自适应资源管控 Agent

更新时间：`2026-07-06`

## 摘要

EulerPilot 是一个面向 openEuler 的自适应资源管控 Agent。项目通过 eBPF 进行低开销观测，在用户态完成 workload 分类、压力识别和策略决策，并通过 `cgroup v2` 与 `sched_ext/scx` 双执行后端实现资源调控。当前项目已经完成从系统观测、策略执行到正式实验、Web Console 展示和中文报告输出的完整工程闭环，并分别在 Redis 与 Nginx 两条业务线上形成 `RUNS=5` 的候选结果目录，在 SP4 自编译 sched_ext 内核上形成 `RUNS=3` 复核结果。

EulerPilot 不追求无条件替代 Linux 默认调度器，而是面向混部干扰场景，通过 eBPF/PSI 感知 workload 状态，在延迟敏感服务与后台干扰共存时，选择 cgroup v2 或 sched_ext/scx 后端进行按需控制，并通过 Redis/Nginx 对照实验展示收益、边界与可回滚能力。

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
- `SP4` 的自编译 sched_ext 内核复核

### 3.2 PsiGate v1 分层门控

当前 `PsiGate v1` 已完成远端闭环验证，状态机包括：

- `NORMAL`
- `ARMED`
- `ACTIVE`
- `COOLDOWN`

其设计目的并不是把 PSI 当作业务退化标签，而是将 PSI 与调度等待、后台运行时等证据结合，用于识别需要加强控制的压力窗口。

PsiGate v1 的核心思想是"压力证据驱动的按需调度激活"：

| 状态 | 触发条件 | 行为 | 退出条件 |
|------|----------|------|----------|
| NORMAL | 无压力证据 | 不激活 scx gate | latency wait / PSI 升高 |
| ARMED | 单周期压力证据出现 | 等待连续确认，避免误触发 | 压力消失或连续成立 |
| ACTIVE | 连续压力证据成立 | 激活 scx 调度策略 | 压力下降 |
| COOLDOWN | 压力刚消退 | 防止抖动反复切换，冷却后回 NORMAL | 冷却结束 |

该机制避免了 sched_ext 长期常驻带来的基础性能开销，也避免了在无干扰场景下误触发调度策略。

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
| `network_policy_demo` | 兼容保留的 `cgroup/connect4` eBPF 网络策略演示 |

该框架已通过 `--list-skills` 和 `--doctor-skills` 命令行验证，并在 `openEuler SP3`、`OLK-6.6` 和 `SP4` 环境编译运行或复核通过。

### 3.5 Network Policy Demo — eBPF 扩展示例

为验证 Skills 框架的可扩展性和赛题对 eBPF 作为 hook 实现 network policy agent 的要求，项目实现了三个可独立验证的网络策略子能力：

| 组件 | 内容 |
|------|------|
| connect4 策略 | `cgroup/connect4`，对目标 cgroup 的动态端口执行 deny |
| TC QoS | `tc_egress` BPF classifier 统计命中，TBF qdisc 执行限速 |
| XDP 策略 | isolated veth 与真实 Pod host veth 上挂 generic XDP，执行 ICMP + TCP + UDP + UDP tuple 四规则 drop/pass，支持协议、源/目的 IP、源/目的端口匹配，并输出聚合与 per-rule 字段统计 |
| Pod target 解析 | `k8s_pod -> kubectl Pod UID/container ID -> runtime PID -> netns -> host veth/ifindex`，`network_qos/network_xdp` 可解析 Pod target 到 host veth |
| 生命周期 | YAML v2 驱动启用 -> attach -> 验证 -> rollback detach -> 恢复 |
| 验证结果 | connect4 deny/recover、TC QoS rollback、XDP drop/recover 均通过 |

TC QoS 还完成了速率误差 Benchmark：在 2 Mbit/s 目标下，121 实测 1.976 Mbit/s（误差 -1.22%），122 实测 1.971 Mbit/s（误差 -1.45%）。这证明 Network Skill 不只是 attach 演示，也能输出可量化的策略效果数据。

这些演示证明：新增一个 eBPF Skill 只需补充 BPF 程序、Skill 适配器和 `skills.yaml` 配置，无需改动 Agent 核心 Runtime 流程。
### 3.6 Security Policy Skill — BPF LSM 安全策略 Agent

为补齐赛题对 "eBPF 作为 hook 实现 security policy agent" 的要求，项目已经从单路径 demo 扩展为正式 `security_policy` Skill，并保留 `security_policy_demo` 作为兼容回归入口。

| 组件 | 内容 |
|------|------|
| BPF 程序 | `lsm/file_open`、`lsm/bprm_check_security`、`lsm/socket_connect`、`lsm/ptrace_traceme`、`lsm/capable`，并补充 `execve/openat/connect/ptrace` tracepoint audit |
| 控制方式 | YAML v2 `targets + rules + target_ref` 下发，用户态填充最多 8 项 BPF `target_map` |
| 文件策略 | `path/path_prefix + file_access=any/read/write`，已验证目标 cgroup 内读打开成功、精确路径写打开和只读目录前缀写打开被拒绝 |
| 执行策略 | 精确 `exec_path` 和字面 `exec_prefix`，已验证目标 cgroup 内可写目录前缀执行被拒绝 |
| 网络安全策略 | `dst_ip + dst_port + cgroup_id`，已验证 scoped IPv4 socket connect 被拒绝 |
| Ptrace 策略 | scope-only cgroup target，已验证目标 cgroup 内 `PTRACE_TRACEME` 被拒绝，scope 外允许 |
| Capability 策略 | `capability + cgroup_id`，已验证目标 cgroup 内 `CAP_SYS_ADMIN` 被拒绝，scope 外允许 |
| Scope | 显式 cgroup、PID 自动解析、container_id cgroup tree 扫描、runtime container name、Kubernetes Pod 名称解析 |
| 生命周期 | YAML 驱动启用 -> LSM/tracepoint attach -> audit/enforce 验证 -> rollback detach -> 恢复 |
| 验证结果 | 121/122 均通过完整集成测试；最新证据目录为 `results/security_policy/integration-20260623-152931` 和 `results/security_policy/integration-20260623-153318` |
| 安全设计 | 默认 disabled，audit 默认不阻断；enforce 只对显式 target 生效，不 pin link 到 BPF 文件系统，Agent 退出即 detach |

该能力证明 Security 类 eBPF hook 可以复用 EulerPilot Skills 框架完成策略声明、目标解析、内核 hook attach、事件审计、阻断验证和可回滚恢复。事件文件 `reports/events/security_policy.jsonl` 会记录 `rule_id/target_ref/target_index/cgroup_id`，并对 socket、exec_prefix、file_access/path_prefix、ptrace 和 capability 分别输出 endpoint、前缀、`file_flags`、`ptrace_traceme` 和 `CAP_*` 证据。


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

### 4.3 双环境交付定位

| 环境 | 内核 | sched_ext | 角色 | 交付定位 |
|------|------|-----------|------|----------|
| 121 / SP3 主环境 | openEuler 24.03 LTS SP3 官方内核 | 不可用 | cgroup v2 主闭环、代码、文档、Dashboard、质量门禁 | **主交付** |
| 122 / OLK-6.6 验证环境 | 6.6.0-olk66-scx | 可用 | sched_ext/scx 正式 compare、class_map、PsiGate 验证 | **增强验证** |
| 123 / SP4 复核环境 | openEuler 24.03 LTS SP4，自编译 `eulerpilot-scx` 内核 | 可用 | SP4 sched_ext、Redis/Nginx RUNS=3、Web Console 与最终门禁复核 | **平台复核** |

`cgroup v2` 是当前 SP3 上的正式主交付路径；`sched_ext/scx` 已在 OLK-6.6 和 SP4 自编译内核环境完成增强验证，用于证明 Agent 架构可以对接用户态调度后端。

---

## 5. Redis 正式实验

当前建议直接引用的 Redis 候选结果目录为：

- `/root/EulerPilot/results/final/redis-scx-compare-20260612-191543`
- SP4 复核目录：`/root/EulerPilot/results/final/redis-scx-compare-20260706-115029`

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
- SP4 复核目录：`/root/EulerPilot/results/final/nginx-scx-compare-20260706-120547`

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

### 6.3 Nginx 场景下的策略适配边界

Nginx 结果不是框架不可用，而是当前 `scx_eulerpilot` v1 策略对短连接、高频唤醒型 workload 的适配不足。该结果暴露了策略边界，也说明 EulerPilot 需要按 workload 类型选择不同执行后端和参数：

| 维度 | Redis | Nginx | 影响 |
|------|-------|-------|------|
| 连接模型 | 长连接、少量 fd | 短连接、高频 accept | scx DSQ 上下文切换开销放大 |
| 唤醒模式 | 少量 client 唤醒 | wrk 高频并发唤醒 | scx 调度延迟放大为 P99 |
| 请求粒度 | 微秒级内存操作 | 网络 I/O + 文件操作 | scx 常驻 base cost 占比更高 |
| 当前结论 | 正收益可复现 | 策略适配不足 |

因此 Nginx 结果表明：EulerPilot 框架已成功迁移到第二条业务线并获得可对比性能证据，但不同 workload 对同一 sched_ext 策略的敏感性差异显著——后续应按 workload 类型差异化配置调度参数。

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

## 8. 结果边界与定位

EulerPilot 的价值不在于证明某一组参数在所有场景下都优于默认调度器，而在于：

1. 完成从 eBPF 观测到双后端执行的完整工程闭环
2. 在 Redis 混部场景下实现可重现的尾延迟保护
3. 在 Nginx 场景下暴露 sched_ext 策略的适配边界
4. 通过 Skills 框架证明新增 OS Agent 能力的扩展性
5. 通过质量门禁、双环境回归和 SP4 平台复核证明工程可靠性

### 8.1 最终数据采用规则

1. 正文主结论以最新完成的正式实验轮次为准
2. 历史候选结果（含 `RUNS=5`）作为参考保留，不作为正文主结论
3. 报告中保留全部轮次的 `run_manifest`、原始 CSV、`summary.json`
4. 结论优先使用 median 与方向一致的指标，不只挑单项最优值
5. 不同 workload 的结果按各自场景分别讨论，不做跨场景简单平均

但最终报告中不应写成：

- "sched_ext 全面优于默认调度器"
- "所有模式都稳定带来收益"

更稳的结论应是：

> EulerPilot 已经完成从系统实现到正式 compare 的工程收口，当前结果表明，不同 `sched_ext` 模式在不同 workload 上的收益与代价具有明显场景差异，项目的价值主要体现在统一 Agent 架构、双后端正式实验能力、Skills 插件化扩展能力、以及可复现证据链。

---

## 9. 当前结论

1. EulerPilot 已在 `SP3` 上完成 cgroup v2 主闭环，具备正式交付能力。
2. EulerPilot 已在 `OLK-6.6` 上完成 Redis 与 Nginx 的 `sched_ext` 正式 compare，并在 `SP4` 自编译 sched_ext 内核上完成 Redis/Nginx `RUNS=3` 复核。
3. EulerPilot 已实现 Skills 插件化框架与 YAML v2 驱动，并通过 `network_policy`、`network_qos`、`network_xdp` 和正式 `security_policy` 证明了 Agent 能力可扩展。
4. 项目已通过最新 SP4 质量门禁（`results/k8s/sp4-validation-20260708-023552/final_quality_gate.log`，22/22 P0、100 轮 smoke、5 轮 doctor）和安全审计，并形成 35 条 final evidence compact。

补充说明：

- `cgroup v2` 是当前 SP3 上的正式主交付路径；`sched_ext/scx` 是在 OLK-6.6 与 SP4 自编译内核环境完成的增强验证线。
- 当前建议提交时以 `192.168.1.123:/root/EulerPilot` 作为 SP4 主验证和最终交付验证目录；`192.168.1.121:/root/EulerPilot` 作为 SP3 历史验证和回归对照目录保留。
- 项目代码已同步推送至 GitHub 私密仓库 `shibuchou/EulerPilot`。

项目代码已同步推送至 GitHub 私密仓库 `shibuchou/EulerPilot`。

当前项目已覆盖 resource control、network policy、security policy 三类 OS Agent 扩展方向：其中 resource control 进入 Redis/Nginx 主实验路径，network policy 已具备 connect4、TC QoS、XDP 三个可验证子能力，XDP 已支持 ICMP/TCP/UDP 与 UDP tuple 多字段规则和 per-rule 字段统计，security policy 已从独立 demo 升级为正式 Skill，覆盖 file、exec、socket、ptrace 四类 eBPF/LSM hook 的 audit、deny、rollback、recover 可演示闭环。
