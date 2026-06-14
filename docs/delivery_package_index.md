# EulerPilot 最终交付包索引

更新时间：`2026-06-12`

## 1. 项目定位

EulerPilot 是一个面向 openEuler 的自适应资源管控 Agent。

当前项目已经形成两条可交付主线：

- `SP3 + cgroup v2` 主闭环
- `OLK-6.6 + sched_ext` 正式对照线

当前不再缺核心实现与正式实验目录，当前索引的作用是：

> 让提交材料、结果目录、图表和正文文档都能从一个入口被快速找到。

说明：

- 文中提到的最终候选结果目录与图表目录位于远端验证机 `192.168.1.122` 的 `/root/EulerPilot` 下。
- 本地当前主要维护代码与文档入口。

---

## 2. 核心代码入口

### 2.1 Agent 与执行后端

- `/root/EulerPilot/agent`
- `/root/EulerPilot/bpf`
- `/root/EulerPilot/sched`

### 2.2 Redis 正式实验脚本

- `/root/EulerPilot/bench/redis/run_redis_main_experiment.sh`
- `/root/EulerPilot/bench/redis/run_redis_sched_ext_compare.sh`

### 2.3 Nginx 正式实验脚本

- `/root/EulerPilot/bench/nginx/run_nginx_main_experiment.sh`
- `/root/EulerPilot/bench/nginx/run_nginx_sched_ext_compare.sh`

---

## 3. 最终候选结果目录

### 3.1 Redis

建议最终正文优先引用：

- `/root/EulerPilot/results/final/redis-scx-compare-20260612-191543`

目录内关键文件：

- `run_manifest.json`
- `compare_summary_avg.csv`
- `report.md`
- `summary.md`
- `run-1` 到 `run-5`

### 3.2 Nginx

建议最终正文优先引用：

- `/root/EulerPilot/results/final/nginx-scx-compare-20260612-194018`

目录内关键文件：

- `run_manifest.json`
- `compare_summary_avg.csv`
- `report.md`
- `summary.md`
- `run-1` 到 `run-5`

---

## 4. 图表材料

当前图表统一位于：

- `/root/EulerPilot/reports/final_figures`

当前已生成：

- `/root/EulerPilot/reports/final_figures/redis_sched_ext_rps.svg`
- `/root/EulerPilot/reports/final_figures/redis_sched_ext_p99.svg`
- `/root/EulerPilot/reports/final_figures/nginx_sched_ext_rps.svg`
- `/root/EulerPilot/reports/final_figures/nginx_sched_ext_p99.svg`
- `/root/EulerPilot/reports/final_figures/redis_quiet_overhead.svg`
- `/root/EulerPilot/reports/final_figures/nginx_quiet_overhead.svg`
- `/root/EulerPilot/reports/final_figures/psigate_timeline.svg`

---

## 5. 正文文档

### 5.1 当前最重要的中文说明文档

- `/root/EulerPilot/docs/project_status_overview.md`
- `/root/EulerPilot/docs/experiments.md`
- `/root/EulerPilot/docs/stage_delivery_summary.md`
- `/root/EulerPilot/docs/final_results_summary.md`

### 5.2 最终报告相关文档

- `/root/EulerPilot/docs/final_report_outline.md`
- `/root/EulerPilot/docs/final_report_draft.md`

### 5.3 提交整理文档

- `/root/EulerPilot/docs/submission_checklist.md`
- `/root/EulerPilot/docs/delivery_package_index.md`

---

## 6. 当前建议的提交组合

如果现在要整理比赛提交材料，建议按下面组合组织：

### 6.1 技术报告正文主依据

- `/root/EulerPilot/docs/final_report_draft.md`

### 6.2 结果摘要主依据

- `/root/EulerPilot/docs/final_results_summary.md`

### 6.3 阶段与环境说明主依据

- `/root/EulerPilot/docs/stage_delivery_summary.md`

### 6.4 图表主依据

- `/root/EulerPilot/reports/final_figures`

### 6.5 Redis 与 Nginx 正文候选结果

- `/root/EulerPilot/results/final/redis-scx-compare-20260612-191543`
- `/root/EulerPilot/results/final/nginx-scx-compare-20260612-194018`

---

## 7. 当前还需要人工处理的最后事项

当前已经不再缺：

- 核心代码
- 正式实验脚本
- Redis / Nginx 多轮候选结果
- 图表材料
- 中文草稿文档

当前真正还需要人工处理的内容主要是：

- 把 `final_report_draft.md` 继续润色成最终提交正文
- 把图表插入正式报告并统一排版
- 再做一页更精简的答辩展示摘要

---

## 8. 一句话结论

当前可以把项目状态概括为：

> EulerPilot 已经完成从系统实现、双后端正式实验、候选结果目录、图表材料到中文报告草稿的整套交付链，剩余工作已经收敛为最终文字润色与展示材料整理。
