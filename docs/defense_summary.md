# EulerPilot 答辩展示摘要

更新时间：`2026-07-17`

## 1. 一句话介绍

EulerPilot 是一个面向 openEuler 的自适应资源管控 Agent，通过 `eBPF` 感知 workload 特征，结合分层决策与 `cgroup v2 / sched_ext` 双后端执行，实现对延迟敏感任务的保护与后台干扰任务的抑制。

## 2. 当前做成了什么

- `SP3 + cgroup v2` 主闭环
- `OLK-6.6 + sched_ext` 正式对照线
- `SP4 + 自编译 sched_ext 内核` 增强复核线
- `PsiGate v1` 门控状态机
- Skills 插件化框架（Resource / Network / Security / Policy Engine 等正式 Skill + YAML 驱动）
- Network Policy：connect4、TC QoS、XDP、多字段 tuple、真实 Pod host veth
- Security Policy：LSM enforce、syscall tracing、服务联动 anomaly、credential anomaly/deep hook 评估
- Resource Control：CPU + Memory + IO 自动闭环，真实 container / k3s Pod target
- Policy Engine：Security anomaly 驱动 Resource、Network+Resource、真实 Pod 联动
- Redis `RUNS=5` 正式候选结果
- Nginx `RUNS=5` 正式候选结果
- SP4 Redis/Nginx `RUNS=5` sched_ext 多轮复核
- Redis pressure gradient 与 manual static vs agent dynamic 对比
- Web Console v1 + 37 条 final evidence compact + 中文报告主稿

## 3. 核心架构

```text
Observer -> Analyzer -> Policy Engine -> Skill Manager -> Executor -> Benchmark/Report
```

三个 OS Agent 扩展方向全覆盖：

| 方向 | 实现 | 状态 |
|------|------|------|
| resource control | CgroupExecutor + ScxExecutor + CPU/Memory/IO + runtime/Pod target | 主线 |
| network policy | connect4 + TC QoS + XDP + Pod host veth | 已完成 |
| security policy | BPF LSM + syscall tracing + anomaly + credential hooks | 已完成 |
| policy engine | Security anomaly -> Resource / Network+Resource / real Pod 联动 | 已完成 |

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
- `sched_ext` 不应表述为对所有 workload 都稳定提升
- SP4 追加复核：`results/final/redis-scx-compare-20260708-150702`，`RUNS=5`
- 争奖增强证据：`results/final/redis-pressure-gradient-20260708-153811`、`results/final/redis-static-vs-agent-20260708-162543`

### Nginx（`nginx-scx-compare-20260612-194018`）
- `cgroup_v2` 在 Nginx 场景下更稳
- `sched_ext` 效果依赖具体模式，部分模式存在显著尾延迟代价
- SP4 追加复核：`results/final/nginx-scx-compare-20260708-152602`，`RUNS=5`

## 6. 图表材料（7 张 SVG）

`redis_sched_ext_rps / p99 / quiet_overhead`，`nginx_sched_ext_rps / p99 / quiet_overhead`，`psigate_timeline`

## 7. 可答辩口径

> EulerPilot 已经完成从 SP3 + cgroup v2 历史主闭环到 SP4 发行环境适配、SP4 官方源码自编译 sched_ext 内核复核与 Kubernetes 旁路验证的完整工程收口，并实现了 Skills 框架、三方向 OS Agent 扩展和跨 Skill 联动。项目已形成 Redis/Nginx 双业务线多轮候选结果、37 条 final evidence compact、质量门禁和 Web Console 演示入口，具备真实可复现的系统调控能力。

项目代码：`https://github.com/shibuchou/EulerPilot`（私密仓库）

