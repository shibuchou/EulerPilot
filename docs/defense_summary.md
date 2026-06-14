# EulerPilot 答辩展示摘要

更新时间：`2026-06-12`

## 1. 一句话介绍

EulerPilot 是一个面向 openEuler 的自适应资源管控 Agent，通过 `eBPF` 感知 workload 特征，结合规则决策与 `cgroup v2 / sched_ext` 双后端执行，实现对延迟敏感任务的保护与后台干扰任务的抑制。

说明：

- 文中提到的正式候选结果目录与图表目录位于远端验证机 `192.168.1.122` 的 `/root/EulerPilot` 下。

---

## 2. 当前做成了什么

当前项目已经完成：

- `SP3 + cgroup v2` 主闭环
- `OLK-6.6 + sched_ext` 正式对照线
- Redis `RUNS=5` 正式候选结果
- Nginx `RUNS=5` 正式候选结果
- 中文报告草稿与图表材料

---

## 3. 核心架构

```text
Observer
-> Analyzer
-> Policy Engine
-> Executor
-> Benchmark / Report
```

其中：

- `Observer`：eBPF 调度事件 + PSI
- `Analyzer`：workload 分类 + 压力证据
- `Policy Engine`：分层触发与滞回
- `Executor`：`CgroupExecutor` / `ScxExecutor`

---

## 4. 核心创新点

### 4.1 双后端统一 Agent 架构

同一套 Agent 主体同时服务：

- `SP3 + cgroup v2`
- `OLK-6.6 + sched_ext`

### 4.2 PsiGate 分层门控

当前状态机：

- `NORMAL`
- `ARMED`
- `ACTIVE`
- `COOLDOWN`

### 4.3 正式 compare 实验框架

当前已经具备：

- 多后端矩阵
- 多轮运行
- 平衡轮换
- `run_manifest`
- `invalid_run`
- 中文结果输出

---

## 5. 结果结论

### 5.1 Redis

当前最强候选结果：

- `/root/EulerPilot/results/final/redis-scx-compare-20260612-191543`

当前结论：

- `noisy_cgroup_v2` 与 `noisy_scx_normal` 在部分关键操作上呈现正向趋势
- `noisy_scx_psi` 在部分操作上具备一定正向效果
- `sched_ext` 不应被表述为“全面优于默认调度器”

### 5.2 Nginx

当前最强候选结果：

- `/root/EulerPilot/results/final/nginx-scx-compare-20260612-194018`

当前结论：

- `cgroup_v2` 在当前 Nginx 场景下表现更稳
- `sched_ext` 在 Nginx 上的效果更依赖具体模式
- 某些 `sched_ext` 模式存在显著尾延迟代价

---

## 6. 当前图表材料

当前已经生成：

- `redis_sched_ext_rps.svg`
- `redis_sched_ext_p99.svg`
- `nginx_sched_ext_rps.svg`
- `nginx_sched_ext_p99.svg`
- `redis_quiet_overhead.svg`
- `nginx_quiet_overhead.svg`
- `psigate_timeline.svg`

目录：

- `/root/EulerPilot/reports/final_figures`

---

## 7. 当前可答辩口径

建议当前答辩时使用下面这条口径：

> EulerPilot 已经完成从 `SP3 + cgroup v2` 主闭环到 `OLK-6.6 + sched_ext` 正式 compare 的完整工程收口，并形成了 Redis 与 Nginx 两条业务线的多轮候选结果目录。当前结果表明，该框架已经具备真实可复现的系统调控能力，但 `sched_ext` 在不同 workload 上的收益与代价仍需按具体场景谨慎解释。

---

## 8. 当前还差什么

当前剩余工作已经不再是系统功能，而是提交材料收尾：

- 最终技术报告正文润色
- 图表插入与排版
- 最终答辩展示页美化

也就是说：

> 现在剩下的是“怎么展示”，不是“做不出来”。
