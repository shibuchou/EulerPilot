# EulerPilot 提交清单

更新时间：`2026-06-14`

## 已完成

- [x] `SP3 + cgroup v2` 主闭环
- [x] `OLK-6.6 + sched_ext` 正式对照线
- [x] `PsiGate v1` 闭环验证
- [x] Redis `RUNS=5` 正式候选结果
- [x] Nginx `RUNS=5` 正式候选结果
- [x] Skills 插件化框架（Skill/Registry/Manager/builtin_skills）
- [x] YAML 驱动 Skills 启停与配置（`skills.yaml` -> `agent.yaml` 联动）
- [x] `--list-skills` / `--doctor-skills` 命令行验证（现输出 4 项）
- [x] `network_policy_demo` eBPF cgroup/connect4 最小闭环（attach -> deny -> rollback -> recover）
- [x] `security_policy_demo` BPF LSM file_open 最小闭环（attach -> deny -> rollback -> recover）
- [x] 双环境（121 SP3 + 122 OLK-6.6）编译与回归通过
- [x] 中文阶段交付汇总
- [x] 中文最终报告主稿
- [x] sched_ext 后端对照图（7 张 SVG）
- [x] PsiGate 状态时间线图
- [x] 代码推送至 GitHub 私密仓库 `shibuchou/EulerPilot`

## 当前核心结果目录

- Redis：`/root/EulerPilot/results/final/redis-scx-compare-20260612-191543`
- Nginx：`/root/EulerPilot/results/final/nginx-scx-compare-20260612-194018`

## 当前核心文档

- `/root/EulerPilot/docs/final_report_submission.md` — 最终提交主稿
- `/root/EulerPilot/docs/final_results_summary.md` — 结果摘要
- `/root/EulerPilot/docs/final_talk_script.md` — 答辩讲稿
- `/root/EulerPilot/docs/defense_slides_outline.md` — 答辩页提纲
- `/root/EulerPilot/docs/demo_runbook.md` — 演示运行说明
- `/root/EulerPilot/docs/handover_manual.md` — 项目交接手册

## 当前图表目录

- `/root/EulerPilot/reports/final_figures`（7 张 SVG）

## 赛题覆盖方向

| 方向 | 实现方式 | 状态 |
|------|----------|------|
| resource control agent | `CgroupExecutor + ScxExecutor`，进入 Redis/Nginx 主实验 | ✅ |
| network policy agent | `network_policy_demo`（cgroup/connect4） | ✅ |
| security policy agent | `security_policy_demo`（BPF LSM file_open） | ✅ |

## 当前结论

项目已经完成系统实现、双后端正式实验、Skills 扩展框架、network/security eBPF 演示、图表材料和中文主稿。剩余工作已收敛为：

> 最终中文报告排版与答辩页制作。

项目代码已同步推送至 GitHub：`https://github.com/shibuchou/EulerPilot`（私密仓库）
