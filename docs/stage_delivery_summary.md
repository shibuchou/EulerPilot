# EulerPilot 阶段交付汇总

更新时间：`2026-07-17`

> 本文保留阶段交付脉络。当前最终交付验证线已升级为 `192.168.1.123:/root/EulerPilot` 的 SP4 主验证仓库；121 作为 SP3 历史验证和回归对照，122 作为 SP3/OLK 对照验证。最终状态以 `docs/progress_status.md`、`docs/final_evidence_index.md` 和 `reports/final_evidence_compact.md` 为准。

## 1. 当前项目做到哪了

当前项目已经不再停留在：

- 架构设计
- 单点 smoke
- 单轮试跑

而是已经形成了两条可以真实对外展示的交付线：

1. `SP3 + cgroup v2` 正式主线
2. `OLK-6.6 + sched_ext` 正式对照线

其中当前最重要的里程碑是：

- Redis `sched_ext` 正式对照已经完成 `RUNS=5` 候选结果
- Nginx `sched_ext` 正式对照已经完成 `RUNS=5` 候选结果
- `PsiGate v1` 已完成远端闭环验证
- 两个执行后端都已经具备正式实验脚本、中文报告和结果目录
- Redis / Nginx sched_ext 后端对照图表已经生成

---

## 2. 当前实际使用的环境

### 2.1 SP4 核心验证环境

```text
主机：192.168.1.123
系统：openEuler 24.03 LTS SP4
目录：/root/EulerPilot
定位：核心验证和最终交付验证仓库
```

这台机器当前承担：

- SP4 发行环境适配验证
- SP4 官方源码自编译 sched_ext 内核复核
- Redis/Nginx RUNS=5 复核
- Web Console、Kubernetes 旁路验证和最终质量门禁

### 2.2 SP3 历史验证环境

```text
主机：192.168.1.121
系统：openEuler 24.03 LTS SP3
目录：/root/EulerPilot
定位：SP3 历史验证环境
```

这台机器当前承担：

- `eBPF + Agent + cgroup v2` 主闭环
- Redis / Nginx 主实验
- 早期中文文档与报告主线

### 2.3 sched_ext 提前验证环境

```text
主机：192.168.1.122
hostname：cernet2.net
系统：openEuler 24.03 LTS SP3
内核：6.6.0-olk66-scx
目录：/root/EulerPilot
定位：OLK-6.6 / sched_ext 正式对照验证环境
```

这台机器当前已经完成：

- `OLK-6.6` 内核编译和安装
- `CONFIG_SCHED_CLASS_EXT=y`
- `/sys/kernel/sched_ext` 存在
- `scx_simple` 与 `scx_eulerpilot` 可运行
- Redis / Nginx 的 `sched_ext` 正式 compare

---

## 3. 当前已经完成的核心能力

### 3.1 观测层

当前已经具备：

- `sched_wakeup`
- `sched_switch`
- `sched_migrate_task`

当前已输出的 task 级指标包括：

- `wakeup_count`
- `total_wait_ns`
- `runtime_ns`
- `ctx_switch_count`
- `migrate_count`

### 3.2 Agent 决策层

当前已经具备：

- workload 角色识别
- `latency_exists/background_exists` 前提判断
- `cpu_psi_high/latency_wait_high/background_runtime_high` 压力证据
- `normal / latency / mixed / throughput` 控制级别
- `2 / 2 / 5` 滞回

### 3.3 执行后端

当前已经完成两个后端：

- `CgroupExecutor`
- `ScxExecutor`

其中：

- `CgroupExecutor` 已用于 SP3 正式主线
- `ScxExecutor` 已用于 OLK-6.6 正式 compare

### 3.4 PsiGate

当前 `PsiGate v1` 已完成：

- `loader-only wiring`
- `gate_mode=normal`
- `gate_mode=always-active`
- `gate_mode=psi`
- `redis_only / redis_stress / redis_recover / redis_repeat3`

结论是：

> `PsiGate v1` 已经不是代码骨架，而是通过远端闭环验证的正式模块。

---

## 4. 当前已经具备的正式实验入口

### 4.1 Redis 主线

当前入口包括：

- `bench/redis/run_redis_main_experiment.sh`
- `bench/redis/run_redis_sched_ext_compare.sh`

其中 `run_redis_sched_ext_compare.sh` 当前支持：

- `quiet_default`
- `quiet_scx_normal`
- `noisy_default`
- `noisy_cgroup_v2`
- `noisy_scx_normal`
- `noisy_scx_always_active`
- `noisy_scx_psi`

并且已经支持：

- 平衡轮换顺序
- `run_order.txt`
- `invalid_reason`
- `run_manifest.json`
- 中文报告

### 4.2 Nginx 第二线

当前入口包括：

- `bench/nginx/run_nginx_main_experiment.sh`
- `bench/nginx/run_nginx_sched_ext_compare.sh`

`run_nginx_sched_ext_compare.sh` 当前已经和 Redis 保持统一矩阵与统一结果结构。

---

## 5. 当前最重要的结果目录

### 5.1 Redis sched_ext 正式候选

当前最重要的 Redis 结果目录：

- `/root/EulerPilot/results/final/redis-scx-compare-20260612-185727`
- `/root/EulerPilot/results/final/redis-scx-compare-20260612-191543`

其中：

- `185727`
  - `RUNS=3`
  - 已通过平衡轮换验证
- `191543`
  - `RUNS=5`
  - 已形成当前最接近正文候选的 Redis 正式结果

### 5.2 Nginx sched_ext 正式候选

当前最重要的 Nginx 结果目录：

- `/root/EulerPilot/results/final/nginx-scx-compare-20260612-191150`
- `/root/EulerPilot/results/final/nginx-scx-compare-20260612-193017`
- `/root/EulerPilot/results/final/nginx-scx-compare-20260612-194018`

其中：

- `191150`
  - `RUNS=1`
  - 最小正式验证
- `193017`
  - `RUNS=3`
  - 平衡轮换正式候选结果
- `194018`
  - `RUNS=5`
  - 当前最接近正文候选的 Nginx 正式结果

---

## 6. 当前已经可以确认的结论

当前已经可以确认的不是“性能一定全面领先”，而是下面这些更稳的结论：

### 6.1 工程闭环已经成立

当前已经真正形成：

```text
eBPF 观测
-> Agent 分类/触发
-> CgroupExecutor / ScxExecutor
-> Redis / Nginx 正式实验
-> 中文结果输出
```

### 6.2 sched_ext 正式对照线已经成立

当前已经确认：

- Redis `sched_ext` 正式 compare 可多轮运行
- Nginx `sched_ext` 正式 compare 可多轮运行
- `PsiGate v1` 能在正式 compare 中进入 `ACTIVE`
- `cgroup_v2` 和 `sched_ext` 两种后端都能留下明确执行证据

### 6.3 当前结果已具备答辩展示价值

当前已经能展示：

- 两台环境分工
- 两条业务线
- 两个执行后端
- 中文结果目录
- 关键证据链

---

## 7. 当前还没有完成的部分

当前还不能宣称“项目完全结束”，因为还剩下这些工作：

### 7.1 最终技术报告正文还没完全收口

当前虽然已经有：

- 中文阶段汇总
- 中文最终报告骨架
- 中文最终报告草稿
- 中文最终报告 `v1`

但最终比赛正文仍需要：

- 进一步润色语言
- 统一章节结构
- 固化结论边界
- 插入当前已生成图表
- 补最终版本实验章节

### 7.2 图表材料已基本生成，但还未完成最终排版

当前已经生成：

- `/root/EulerPilot/reports/final_figures/redis_sched_ext_rps.svg`
- `/root/EulerPilot/reports/final_figures/redis_sched_ext_p99.svg`
- `/root/EulerPilot/reports/final_figures/nginx_sched_ext_rps.svg`
- `/root/EulerPilot/reports/final_figures/nginx_sched_ext_p99.svg`
- `/root/EulerPilot/reports/final_figures/redis_quiet_overhead.svg`
- `/root/EulerPilot/reports/final_figures/nginx_quiet_overhead.svg`
- `/root/EulerPilot/reports/final_figures/psigate_timeline.svg`

当前真正还缺的是：

- 把这些图表嵌入最终报告正文
- 统一图标题、图注和版式

### 7.3 SP3 与 OLK-6.6 的最终叙述还需最后整合

当前已经有内容，但最后报告里还要明确：

- `SP3` 回答“官方环境可交付性”
- `OLK-6.6` 回答“sched_ext 正式对照能力”
- `SP4` 回答“最终迁移目标”

---

## 8. 当前最合理的下一步

如果按比赛交付优先级继续推进，当前最合理的顺序是：

1. 冻结 Redis / Nginx 候选结果目录
2. 整理最终中文技术报告正文
3. 把图表插入报告并统一排版
4. 生成更精简的答辩展示材料
5. 最后再决定是否继续做额外调参

---

## 9. 一句话状态

当前可以用一句话概括：

> EulerPilot 已经在 openEuler SP3 上完成了 `eBPF + Agent + cgroup v2` 主闭环，并在 OLK-6.6 上完成了 `sched_ext` 正式对照线；Redis 与 Nginx 都已经拿到 `RUNS=5` 级别的候选结果，项目已经进入最终交付整理阶段，而不再是功能开发阶段。
