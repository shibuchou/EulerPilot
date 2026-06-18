# EulerPilot 一页式简介

更新时间：`2026-06-14`

## 项目是什么

EulerPilot 是面向 openEuler 的自适应资源管控 Agent——一个本地运行的系统自治控制程序：

```
观测系统状态 -> 识别 workload 类型 -> 判断压力场景 -> 选择控制策略 -> 执行 -> 输出结果
```

## 当前做成了什么

- SP3 + cgroup v2 主闭环
- OLK-6.6 + sched_ext 正式对照线
- PsiGate v1 门控状态机
- Skills 插件化框架（4 runtime skills + YAML 驱动）
- network_policy_demo（cgroup/connect4）
- security_policy_demo（BPF LSM file_open）
- Redis RUNS=5 + Nginx RUNS=5 候选结果
- 7 张 SVG 图表 + 中文报告主稿

## 赛题覆盖

| 方向 | 实现 | 状态 |
|------|------|------|
| resource control agent | CgroupExecutor + ScxExecutor | 主线实验 |
| network policy agent | cgroup/connect4 demo | 可演示 |
| security policy agent | BPF LSM file_open demo | 可演示 |

## 核心结果目录
- Redis：`results/final/redis-scx-compare-20260612-191543`
- Nginx：`results/final/nginx-scx-compare-20260612-194018`

## 核心文档
- `docs/final_report_submission.md` — 最终报告主稿
- `docs/defense_summary.md` — 答辩摘要
- `docs/handover_manual.md` — 项目交接手册

## 代码仓库
`https://github.com/shibuchou/EulerPilot`（私密仓库）

## 当前结论
项目已完成系统实现、双后端实验、三方向 OS Agent 扩展、图表材料和中文主稿。剩余工作为报告排版与答辩页制作。
