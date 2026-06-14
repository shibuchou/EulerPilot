# EulerPilot 一页式简介

更新时间：`2026-06-12`

## 项目是什么

EulerPilot 是一个面向 openEuler 的自适应资源管控 Agent。

说明：

- 文中提到的正式候选结果目录与图表目录位于远端验证机 `192.168.1.122` 的 `/root/EulerPilot` 下。

它不是聊天型 AI Agent，而是一个本地运行的系统自治控制程序，负责：

```text
观测系统状态
-> 识别 workload 类型
-> 判断压力场景
-> 选择控制策略
-> 通过 cgroup v2 / sched_ext 执行
-> 输出性能结果
```

## 当前做成了什么

- `SP3 + cgroup v2` 主闭环已完成
- `OLK-6.6 + sched_ext` 正式对照线已完成
- `PsiGate v1` 已完成闭环验证
- Redis `RUNS=5` 正式候选结果已完成
- Nginx `RUNS=5` 正式候选结果已完成
- 图表材料与中文报告主稿已生成

## 当前最重要的结果目录

- Redis：`/root/EulerPilot/results/final/redis-scx-compare-20260612-191543`
- Nginx：`/root/EulerPilot/results/final/nginx-scx-compare-20260612-194018`

## 当前最重要的图表目录

- `/root/EulerPilot/reports/final_figures`

## 当前最重要的文档

- `/root/EulerPilot/docs/final_report_v2.md`
- `/root/EulerPilot/docs/final_results_summary.md`
- `/root/EulerPilot/docs/defense_summary.md`
- `/root/EulerPilot/docs/delivery_package_index.md`

## 当前结论

当前项目已经完成：

- 系统实现
- 双后端正式实验
- 多轮候选结果目录
- 图表材料
- 中文报告主稿

当前剩余工作已经不再是功能实现，而是：

- 最终报告润色
- 图表插入排版
- 答辩展示页整理
