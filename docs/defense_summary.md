# EulerPilot 答辩展示摘要

更新时间：`2026-06-14`

## 1. 一句话介绍

EulerPilot 是一个面向 openEuler 的自适应资源管控 Agent，通过 `eBPF` 感知 workload 特征，结合分层决策与 `cgroup v2 / sched_ext` 双后端执行，实现对延迟敏感任务的保护与后台干扰任务的抑制。

## 2. 当前做成了什么

- `SP3 + cgroup v2` 主闭环
- `OLK-6.6 + sched_ext` 正式对照线
- `PsiGate v1` 门控状态机
- Skills 插件化框架（4 个 runtime skill + YAML 驱动）
- `network_policy_demo`（cgroup/connect4 deny demo）
- `security_policy_demo`（BPF LSM file_open deny demo）
- Redis `RUNS=5` 正式候选结果
- Nginx `RUNS=5` 正式候选结果
- 7 张 SVG 图表 + 中文报告主稿

## 3. 核心架构

```text
Observer -> Analyzer -> Policy Engine -> Skill Manager -> Executor -> Benchmark/Report
```

三个 OS Agent 扩展方向全覆盖：

| 方向 | 实现 | 状态 |
|------|------|------|
| resource control | CgroupExecutor + ScxExecutor | 主线实验 |
| network policy | network_policy_demo | 独立 demo |
| security policy | security_policy_demo | 独立 demo |

## 4. 核心创新点

### 4.1 双后端统一 Agent 架构
同一套 Agent 主体同时服务于 `SP3 + cgroup v2` 和 `OLK-6.6 + sched_ext`。

### 4.2 PsiGate v1
`NORMAL -> ARMED -> ACTIVE -> COOLDOWN` 压力门控，不是简单硬阈值。

### 4.3 Skills 插件化框架
`Skill/Registry/Manager/builtin_skills` + YAML 驱动启停，`--list-skills` 输出 4 项，新增 Skill 不侵入核心 Runtime。

### 4.4 正式 compare 框架
多后端矩阵、多轮运行、平衡轮换、`run_manifest`、`invalid_run`、中文报告。

## 5. 结果结论

### Redis（`redis-scx-compare-20260612-191543`）
- `noisy_cgroup_v2` 与 `noisy_scx_normal` 在部分操作呈现正向趋势
- `sched_ext` 不应表述为"全面优于默认调度器"

### Nginx（`nginx-scx-compare-20260612-194018`）
- `cgroup_v2` 在 Nginx 场景下更稳
- `sched_ext` 效果依赖具体模式，部分模式存在显著尾延迟代价

## 6. 图表材料（7 张 SVG）

`redis_sched_ext_rps / p99 / quiet_overhead`，`nginx_sched_ext_rps / p99 / quiet_overhead`，`psigate_timeline`

## 7. 可答辩口径

> EulerPilot 已经完成从 SP3 + cgroup v2 主闭环到 OLK-6.6 + sched_ext 正式 compare 的完整工程收口，并实现了 Skills 框架和三方向 OS Agent 扩展（resource/network/security）。项目已形成 Redis/Nginx 双业务线多轮候选结果，具备真实可复现的系统调控能力。

项目代码：`https://github.com/shibuchou/EulerPilot`（私密仓库）
