# EulerPilot 项目阶段总览

更新时间：`2026-07-17`

> 本文是阶段总览归档，保留 6 月阶段路线和 RUNS=3 过程记录。当前最终交付口径已切换到 SP4/123 主验证线：Redis/Nginx `RUNS=5`、Redis pressure gradient、Redis static-vs-agent、37 条 final evidence compact、22/22 P0 质量门禁和 Web Console/K8s 旁路验证。最新状态请以 `README.md`、`docs/progress_status.md` 和 `docs/final_evidence_index.md` 为准。

## 1. 这是什么项目

EulerPilot 是一个面向 openEuler 的自适应资源管控 Agent。

如果用一句话解释，这个项目要做的是：

> 让系统先“看懂”当前跑的 workload 是什么，再自动选择合适的资源控制方式，尽量保护关键服务的性能。

它不是聊天机器人，也不是大模型 Agent。
这里的 Agent 更接近“系统自治控制程序”：

```text
观测系统状态
-> 判断 workload 类型
-> 选择控制策略
-> 执行 cgroup 或 sched_ext/scx
-> 反馈实验结果
```

当前项目围绕比赛交付推进，目标不是只写一个调度器 demo，而是交付一套：

- 能运行
- 能测试
- 能演示
- 能写进技术报告
- 后续还能迁移到 SP4

的完整系统。

---

## 2. 我们为什么要做它

这个项目对应的比赛方向是：

- 第三届中国研究生操作系统开源创新大赛
- 系统创新赛道
- 题目：面向 openEuler 的自适应资源管控 Agent

赛题核心要求是：

- 设计并实现一个基于用户态调度的 Agent 框架
- 能识别 workload
- 能执行资源/调度控制
- 最终在 openEuler 上做出可复现的性能对比结果

因此，EulerPilot 当前采用的是“两阶段路线”：

1. 先在当前官方 `SP3` 内核上，把主闭环跑通
2. 再把 `sched_ext/scx` 接进来，准备迁移到 `SP4`

这样做的原因很现实：

- 比赛交付不能被内核条件卡死
- 但也不能完全放弃 `sched_ext/scx` 这条增强路线

---

## 3. 当前有哪些虚拟机和环境

目前项目实际使用了两台关键环境。

### 3.1 主开发/主实验环境

用途：

- 当前主线开发
- `eBPF + Agent + cgroup v2` 主实验
- Redis / Nginx 正式实验
- 中文报告与材料生成

环境信息：

```text
主机：192.168.1.121
系统：openEuler 24.03 LTS SP3
项目目录：/root/EulerPilot
角色：当前正式主交付环境
```

这个环境的特点是：

- 官方 SP3 内核稳定
- `PSI` 和 `cgroup v2` 已可用
- 适合先把“观测 -> 判断 -> cgroup 控制 -> 实验报告”这条主链跑稳

但它的限制也很明确：

- 当前官方 SP3 内核不能直接作为正式 `sched_ext/scx` 主实验环境使用

所以它目前承担的是：

> 比赛当前可交付主线环境

### 3.2 `sched_ext` 独立验证环境

用途：

- 提前验证 `OLK-6.6`
- 提前验证 `sched_ext/scx`
- 提前把 `ScxExecutor` 和 `scx_eulerpilot` 打通
- 为后续迁移 `SP4` 做准备

环境信息：

```text
主机：192.168.1.122
hostname：cernet2.net
系统：openEuler 24.03 LTS SP3
内核源码目录：/root/olk/kernel-OLK-6.6-atomgit
项目目录：/root/EulerPilot
角色：sched_ext/scx 独立验证机
```

这个环境已经完成了：

- `OLK-6.6` 内核编译
- 新内核安装
- 启动到 `6.6.0-olk66-scx`
- 确认 `CONFIG_SCHED_CLASS_EXT=y`
- 确认 `/sys/kernel/sched_ext` 存在
- 确认 `PSI` / `cgroup v2` / `bpftool` 仍正常
- `scx_eulerpilot` loader-only wiring smoke 通过
- `gate_mode=normal` 固定模式回归通过
- `gate_mode=always-active` 固定模式回归通过
- `PsiGate v1` 的 `redis_only / redis_stress / redis_recover / redis_repeat3` smoke 已闭环通过
- Redis `sched_ext` 正式对照入口第一版已跑通

它当前承担的是：

> `sched_ext/scx` 提前验证环境

当前最新的 Redis `sched_ext` 正式对照结果目录为：

- `/root/EulerPilot/results/final/redis-scx-compare-20260612-185007`
- `/root/EulerPilot/results/final/redis-scx-compare-20260612-185727`
- `/root/EulerPilot/results/final/redis-scx-compare-20260612-191543`

这说明 `192.168.1.122` 现在已经不只是做 smoke，也已经进入：

> Redis 多后端正式对照验证环境

### 3.3 两个环境的分工

简单理解：

- `192.168.1.121`
  - 负责“当前比赛主线必须稳”
- `192.168.1.122`
  - 负责“提前把 sched_ext 做出来，并为 SP4 铺路”

这两个环境不是重复建设，而是有明确分工。

---

## 4. 当前系统架构是什么样

项目当前架构可以概括为：

```text
Workloads
-> eBPF Observer
-> Agent Runtime
-> Workload Analyzer
-> Policy Engine
-> Executor
   -> CgroupExecutor
   -> ScxExecutor
-> Benchmark / Report
```

各模块含义如下。

### 4.1 eBPF Observer

作用：

- 在内核侧低开销采集调度行为

当前已经实现的事件包括：

- `sched_wakeup`
- `sched_switch`
- `sched_migrate_task`

当前已经输出的 task 级指标包括：

- `wakeup_count`
- `total_wait_ns`
- `runtime_ns`
- `ctx_switch_count`
- `migrate_count`

### 4.2 Agent Runtime

作用：

- 周期性读取 eBPF 指标
- 读取 PSI
- 组织分类和执行流程

当前已经具备：

- 加载 observer
- 周期运行
- 输出 Analyzer 日志
- 接入双后端选择

### 4.3 Workload Analyzer

作用：

- 判断当前进程更像哪一类 workload

当前已覆盖的主要目标包括：

- `redis-server`
- `nginx`
- `stress-ng`
- `make`
- `sysbench`

当前默认把 workload 大致分成：

- `LATENCY_SENSITIVE`
- `THROUGHPUT_BATCH`
- `BACKGROUND_NOISY`
- `UNKNOWN`

### 4.4 Policy Engine

作用：

- 不直接看单一指标，而是结合多层证据决定是否控制、控制到什么程度

当前采用三层证据逻辑：

第一层：场景前提

```text
latency workload 存在
并且
background workload 存在
```

第二层：压力证据

- `cpu_psi_high`
- `latency_wait_high`
- `background_runtime_high`

第三层：控制级别

- 轻度控制：`latency_profile`
- 强控制：`mixed_profile`

### 4.5 Executor

执行器当前分两条线：

#### `CgroupExecutor`

当前主线后端。

执行方式：

- `cpu.weight`
- `cgroup.procs`

当前 cgroup 层级固定为：

```text
/sys/fs/cgroup/eulerpilot/
  latency/
  batch/
  background/
```

这条线当前最成熟，已经支撑正式主实验。

#### `ScxExecutor`

当前增强线后端。

作用：

- 对接 `sched_ext/scx`
- 把 Agent 分类结果写入 `class_map`
- 让 `scx_eulerpilot` 按类分流到不同 DSQ

这条线当前仍不是正式默认后端，但到 `2026-06-12` 为止已经具备：

- 稳定 attach / detach
- `class_map` / `gate_state_map` / `stats_map` 握手
- `PsiGate v1` 状态机闭环

因此它已经从“只能做可用性验证”进入“可以开展正式对照实验”的阶段。

---

## 5. 在没有 sched_ext 的部分，我们已经完成了什么

这一部分是当前项目最扎实、最可交付的内容。

### 5.1 主闭环已经跑通

当前已经跑通：

```text
eBPF 观测
-> 用户态分类
-> profile 选择
-> cgroup v2 执行
-> Redis / Nginx 实验
-> 自动报告
```

这意味着项目并不是只有架构图，而是已经可以真实做实验。

### 5.2 `PSI` 已接入

当前 `PSI` 采用的是第一阶段实现：

- 用户态 `psi_reader`
- 读取：
  - `/proc/pressure/cpu`
  - `/proc/pressure/memory`
  - `/proc/pressure/io`

当前 `PSI` 的定位不是“单独作为最终结论”，而是：

> 作为压力窗口证据的一部分

### 5.3 Redis 主实验已经打通

当前 Redis 主实验链路已经完整，包括：

- `baseline`
- `default_noisy`
- `active_noisy`

并且已经具备：

- 多轮运行
- 自动汇总
- 自动生成中文报告
- 参数扫描
- 阈值扫描

Redis 相关脚本当前已经比较完善。

### 5.4 Nginx 第二实验线已经打通

当前 Nginx 线已经不是空壳。

已经完成：

- `wrk + nginx + stress-ng` 抗干扰实验链
- Agent 快照采集
- 自动提取指标
- 自动生成中文报告

这意味着项目已经不只有 Redis 一条业务线。

### 5.5 中文文档基础已经成型

当前已经有：

- README
- 项目概述
- 架构设计
- 开发说明
- 环境检查
- OLK 验证记录
- 候选实验总结
- 技术报告草稿

所以当前项目已经形成了比较完整的“代码 + 实验 + 文档”三件套。

---

## 6. `sched_ext` 这部分我们已经做到哪了

这部分是最近推进最快的内容。

### 6.1 环境已经不是问题

`192.168.1.122` 上已经确认：

- `OLK-6.6` 能编
- 新内核能装
- 新内核能启动
- `/sys/kernel/sched_ext` 存在

所以现在不是“能不能用 `sched_ext`”的问题了。

### 6.2 最小 `scx` 示例已经验证

已经跑通过：

- `scx_simple`

这说明：

- `sched_ext` 不是纸面支持
- 用户态加载器和 BPF 调度器都能实际工作

### 6.3 第一版 `ScxExecutor` 已接入 Agent

当前 Agent 已经支持：

```bash
--backend sched_ext
```

并且在 `sched_ext` 后端下，已经可以：

- 启动 `scx_eulerpilot`
- 维护 pinned `class_map`
- 将目标 workload 的 `tgid -> class` 写入 map
- 在 Agent 日志中返回执行证据

### 6.4 第一版 `scx_eulerpilot` 已实现

当前 `scx_eulerpilot` 已补出第一版：

- `scx_eulerpilot.bpf.c`
- `scx_eulerpilot.c`

当前已经有的核心语义包括：

- `latency_dsq`
- `batch_dsq`
- `background_dsq`
- `shared_dsq`
- `class_map`

当前分类值约定为：

- `0 = normal`
- `1 = latency`
- `2 = batch`
- `3 = background`

### 6.5 `class_map` 数据链已经闭环

我们已经在远端验证过：

- `redis-server -> 1`
- `stress-ng -> 3`
- `nginx -> 1`

这说明 Agent 的分类结果已经真正进入了 `sched_ext` 数据面。

### 6.6 `sched_ext smoke` 两条实验线都已打通

当前已经有：

- Redis `sched_ext smoke`
- Nginx `sched_ext smoke`

它们的作用不是给最终结论，而是验证：

- 控制证据是否完整
- `class_map` 是否正确
- `sched_ext` 后端是否稳定

### 6.7 Redis 正式 `sched_ext` compare 已跑通

我们已经用正式实验入口跑出了：

- `BACKEND=sched_ext`
- 多轮
- 自动汇总
- 自动报告

这一步很关键，因为它意味着：

> `sched_ext` 已经从“验证线”进入“正式实验线”

虽然结果不支持对所有 workload 做绝对性能提升承诺，但正式 compare 框架已经打通。

### 6.8 `PsiGate v1` 已通过状态机闭环验证

`PsiGate v1` 当前已经完成四类 smoke：

- `loader-only wiring`
- `gate_mode=normal`
- `gate_mode=always-active`
- `gate_mode=psi`

其中 `gate_mode=psi` 已在 Redis smoke 中验证：

- `redis_only`
  - 不会持续进入 `ACTIVE`
- `redis_stress`
  - 可以进入 `ACTIVE`
- `redis_recover`
  - 可以从 `ACTIVE -> COOLDOWN -> NORMAL`
- `redis_repeat3`
  - 已验证三轮重复注入后仍能稳定：
    - `ACTIVE -> COOLDOWN`
    - 最终回到 `NORMAL`
    - `detach` 后 `sched_ext state=disabled`
    - `nr_rejected=0`

这意味着：

> `PsiGate v1` 现在已经不是代码骨架，而是一个完成了远端功能闭环验证的可实验模块。

### 6.9 Redis `sched_ext` 正式对照入口第一版已可用

当前已经补出并验证：

- `bench/redis/run_redis_sched_ext_compare.sh`

它当前固定的正式矩阵包括：

- `quiet_default`
- `quiet_scx_normal`
- `noisy_default`
- `noisy_cgroup_v2`
- `noisy_scx_normal`
- `noisy_scx_always_active`
- `noisy_scx_psi`

这条入口已经在 `192.168.1.122` 上完成首轮小规模正式 compare，生成了：

- `run_manifest.json`
- `compare_summary_avg.csv`
- `report.md`
- `summary.md`
- 每组对应的：
  - `agent_snapshot`
  - `summary.csv`
  - `scx_stats.json`
  - `gate_status`
  - `psi_gate_trace`

并且当前已经进一步完成：

- `RUNS=3`
- 平衡轮换顺序记录
- `run_manifest.json` 中保留 `run_orders`
- 当前无 `invalid_run`

并且在最新一轮已经进一步完成：

- `RUNS=5`
- 五轮结果目录均完整生成
- 五轮 `noisy_scx_psi` 均保留 `gate_state=2` 证据

并且已经验证到的关键证据包括：

- `noisy_scx_psi` 在正式 compare 中真实进入 `ACTIVE`
- `noisy_cgroup_v2` 在正式 compare 中存在 `applied=yes reason=assigned`
- `quiet_scx_normal / noisy_scx_normal` 均存在 `executor=sched_ext` 运行证据

其中在 `20260612-185727` 这轮 `RUNS=3` 结果中：

- 三轮 `noisy_scx_psi` 都存在 `ACTIVE` 命中
- 三轮结果均未产生 `*_invalid_reason.txt`

其中在 `20260612-191543` 这轮 `RUNS=5` 结果中：

- 五轮结果均未产生 `*_invalid_reason.txt`
- `compare_summary_avg.csv` 与 `report.md` 已完整生成

这意味着：

> Redis `sched_ext` 正式入口已经从“临时脚本”提升为“可复用实验基线”。

### 6.10 Nginx `sched_ext` 正式对照入口第一版已可用

当前已经补出并验证：

- `bench/nginx/run_nginx_sched_ext_compare.sh`

它和 Redis 正式对照保持同一套后端矩阵：

- `quiet_default`
- `quiet_scx_normal`
- `noisy_default`
- `noisy_cgroup_v2`
- `noisy_scx_normal`
- `noisy_scx_always_active`
- `noisy_scx_psi`

这条入口已经在 `192.168.1.122` 上完成：

- `RUNS=1` 最小正式验证
- `RUNS=3` 平衡轮换正式候选验证
- `RUNS=5` 正文候选长跑验证

当前结果目录为：

- `/root/EulerPilot/results/final/nginx-scx-compare-20260612-191150`
- `/root/EulerPilot/results/final/nginx-scx-compare-20260612-193017`
- `/root/EulerPilot/results/final/nginx-scx-compare-20260612-194018`

并已生成：

- `run_manifest.json`
- `compare_summary_avg.csv`
- `report.md`
- `summary.md`

当前已确认的关键证据包括：

- `noisy_scx_psi` 在正式 compare 中存在 `ACTIVE` 命中
- `noisy_cgroup_v2` 存在 `applied=yes reason=assigned`
- `noisy_scx_psi` 存在 `executor=sched_ext` 运行证据

并且在最新一轮已经进一步完成：

- `RUNS=3`
- 平衡轮换顺序记录
- 当前无 `invalid_run`
- 三轮 `noisy_scx_psi` 均保留 `gate_state=2`

并且在当前最新一轮已经进一步完成：

- `RUNS=5`
- 五轮结果目录均完整生成
- 五轮 `noisy_scx_psi` 均保留 `gate_state=2`

这意味着：

> Nginx `sched_ext` 线也已经从 smoke 升级为正式 compare 入口。

---

## 7. 当前我们完成了哪些实验

### 7.1 Redis + stress-ng

这是当前主实验。

目标：

- 保护 Redis
- 压制后台干扰
- 观察吞吐和尾延迟变化

当前状态：

- `cgroup v2` 正式实验已多轮完成
- `sched_ext` 正式多轮 compare 已跑通
- Redis `sched_ext` 已有 `RUNS=5` 候选结果目录

### 7.2 Nginx + stress-ng

这是第二业务线。

目标：

- 验证项目不是只对 Redis 有效
- 证明框架具备迁移性

当前状态：

- `cgroup v2` 正式实验已跑通
- `sched_ext smoke` 已跑通
- 正式 `sched_ext` 多轮 compare 已跑通
- Nginx `sched_ext` 已有 `RUNS=5` 候选结果目录

---

## 8. 当前结果说明了什么

### 8.1 `cgroup v2` 主线已经可交付

这条线已经具备：

- 真实运行
- 多轮实验
- 中文报告
- 演示材料基础

如果比赛现在就要交阶段结果，这条线已经能支撑。

### 8.2 `sched_ext` 线已经进入“正式候选”阶段

它已经不再只是：

- 环境编译成功
- 最小示例能跑

而是已经具备：

- Agent 接入
- `class_map` 闭环
- Redis 正式 compare

### 8.3 但 `sched_ext` 的结果仍需谨慎解释

当前 `sched_ext` 结果说明：

- 它已经形成了 Redis 与 Nginx 两条业务线的多轮候选结果目录
- 它在部分 workload / 操作上表现出正向趋势
- 但并不能被绝对概括为“全面稳定优于当前 `cgroup v2`”

所以当前最合理的结论是：

> `sched_ext` 已经完成正式 compare 工程收口，但最终报告中仍需按具体 workload 解释收益与代价。

---

## 9. 当前正在完成的部分

目前项目不再处于“搭框架”阶段，而是在做“收口和优化”。

当前正在进行的重点有：

### 9.1 继续补正文材料

包括：

- 更正式的报告表述
- 图表排版与图注
- 后端对比说明

---

## 10. 未来规划是什么

下面是最现实、最清晰的下一阶段路线。

### 阶段 A：统一比赛最终交付

最终要形成：

- 一套代码
- 两个执行后端
- 统一实验脚本
- 统一中文报告
- 统一演示路线

### 阶段 B：迁移到 `SP4`

等 `SP4` 发布后，目标不是重写，而是迁移：

1. 检查 `CONFIG_SCHED_CLASS_EXT`
2. 重新编译 `scx_eulerpilot`
3. 跑 smoke
4. 跑正式 compare
5. 决定是否让 `sched_ext` 成为正式主后端

---

## 11. 当前最准确的项目状态总结

如果让一个不熟悉项目的人快速理解当前状态，可以这样说：

> EulerPilot 当前已经在 openEuler SP3 上完成了基于 eBPF 观测、用户态 Agent 分类和 cgroup v2 执行的完整主闭环，并已跑通 Redis 与 Nginx 两条实验线；与此同时，项目也在独立 `OLK-6.6` 验证环境上完成了 `sched_ext` 基础能力验证、`ScxExecutor` 第一版接入、`class_map -> scx_eulerpilot` 链路验证以及 Redis 正式多轮 compare。  
> 也就是说，当前项目的主交付线已经可运行、可实验、可报告；`sched_ext` 增强线也已经形成 Redis 与 Nginx 两条业务线的正式候选结果目录，当前主要剩余工作已经收敛为最终报告、图表和答辩材料的整理，以及后续向 `openEuler 24.03-LTS-SP4` 的迁移收口。

---

## 12. 当前建议怎么继续看这个项目

如果后续有新同学或老师来了解项目，推荐按这个顺序看：

1. 先看 [README.md](/root/EulerPilot/README.md)
2. 再看 [docs/project_brief.md](/root/EulerPilot/docs/project_brief.md)
3. 再看 [docs/architecture.md](/root/EulerPilot/docs/architecture.md)
4. 再看 [docs/olk_validation.md](/root/EulerPilot/docs/olk_validation.md)
5. 最后看 `results/reports/` 下最近几轮结果

如果要快速看代码主线，推荐按这个顺序：

1. `bpf/workload_observer.bpf.c`
2. `agent/src/runtime.cpp`
3. `agent/src/executors.cpp`
4. `sched/scx_eulerpilot.bpf.c`
5. `bench/redis/` 和 `bench/nginx/`

---

## 13. 当前文档目的

这份文档的目的不是替代技术报告，而是回答下面几个问题：

- 我们这个项目到底在做什么
- 现在有几个环境，分别干什么
- 哪些已经做完了
- 哪些还在进行中
- 后面会怎么走到最终项目成型

如果后续项目继续推进，这份文档也应该持续更新，作为：

> 项目阶段状态总览文档
