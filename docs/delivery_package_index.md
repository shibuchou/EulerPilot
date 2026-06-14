# EulerPilot 最终交付包索引

更新时间：`2026-06-14`

## 1. 项目定位

面向 openEuler 的自适应资源管控 Agent。已形成两条可交付主线：
- SP3 + cgroup v2 主闭环
- OLK-6.6 + sched_ext 正式对照线

## 2. 核心代码

- `agent/src/` — Agent 主体（main/runtime/executors/psi_gate/builtin_skills）
- `bpf/` — eBPF 程序（observer/network_policy_demo/security_policy_demo）
- `sched/` — sched_ext 调度器
- `configs/` — YAML 配置（agent/policy/psi_gate/skills）

## 3. 最终候选结果
- Redis：`results/final/redis-scx-compare-20260612-191543`（RUNS=5）
- Nginx：`results/final/nginx-scx-compare-20260612-194018`（RUNS=5）

## 4. 图表材料
`reports/final_figures/` — 7 张 SVG

## 5. 文档主链
- `docs/final_report_submission.md` — 最终报告主稿
- `docs/final_results_summary.md` — 结果摘要
- `docs/defense_summary.md` — 答辩摘要
- `docs/handover_manual.md` — 项目交接手册
- `docs/submission_checklist.md` — 提交清单

## 6. 代码仓库
`https://github.com/shibuchou/EulerPilot`（私密仓库）

## 7. 当前结论
项目已完成系统实现、双后端实验、三方向 OS Agent 扩展、图表材料和中文主稿。已进入提交冻结阶段。
