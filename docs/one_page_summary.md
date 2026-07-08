# EulerPilot 一页式简介

更新时间：`2026-07-06`

## 项目是什么

EulerPilot 是面向 openEuler 的自适应资源管控 Agent——一个本地运行的系统自治控制程序：

```
观测系统状态 -> 识别 workload 类型 -> 判断压力场景 -> 选择控制策略 -> 执行 -> 输出结果
```

## 当前做成了什么

- SP3 + cgroup v2 主闭环
- OLK-6.6 + sched_ext 正式对照线
- SP4 + 自编译 sched_ext 内核增强复核线
- PsiGate v1 门控状态机
- Skills 插件化框架（Resource / Network / Security / Policy Engine）
- Network Policy：connect4、TC QoS、XDP、真实 Pod host veth
- Security Policy：LSM、syscall tracing、服务联动 anomaly、credential anomaly
- Resource Control：CPU + Memory + IO 自动闭环，真实 container / Pod target
- Policy Engine：跨 Skill 决策、审计、失败回滚和 Agent stop rollback
- Redis RUNS=5 + Nginx RUNS=5 候选结果
- SP4 Redis/Nginx RUNS=5 sched_ext 复核结果
- Web Console v1 + 37 条 final evidence compact + 中文报告主稿

## 赛题覆盖

| 方向 | 实现 | 状态 |
|------|------|------|
| resource control agent | CPU/Memory/IO + cgroup/scx + runtime/Pod target | 已完成 |
| network policy agent | connect4 + TC QoS + XDP + Pod host veth | 已完成 |
| security policy agent | BPF LSM + syscall tracing + anomaly | 已完成 |

## 核心结果目录
- Redis：`results/final/redis-scx-compare-20260612-191543`
- Nginx：`results/final/nginx-scx-compare-20260612-194018`
- SP4 Redis：`results/final/redis-scx-compare-20260708-150702`
- SP4 Nginx：`results/final/nginx-scx-compare-20260708-152602`
- Evidence：`reports/final_evidence_compact.md`

## 核心文档
- `docs/final_report_submission.md` — 最终报告主稿
- `docs/defense_summary.md` — 答辩摘要
- `docs/handover_manual.md` — 项目交接手册

## 代码仓库
`https://github.com/shibuchou/EulerPilot`（私密仓库）

## 当前结论
项目已完成系统实现、双后端实验、SP4 增强复核、三方向 OS Agent 扩展、跨 Skill 联动、Web Console、图表材料和中文主稿。剩余工作集中在答辩页视觉打磨与现场演示彩排。

