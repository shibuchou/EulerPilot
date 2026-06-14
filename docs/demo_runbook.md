# EulerPilot 演示运行说明

更新时间：`2026-06-12`

## 1. 演示目标

演示时建议回答三件事：

1. EulerPilot 能否真实运行。
2. EulerPilot 是否具备正式 compare 能力。
3. EulerPilot 是否已经形成 Redis / Nginx 双业务线结果。

说明：

- 下文提到的正式候选结果目录与图表目录位于远端验证机 `192.168.1.122` 的 `/root/EulerPilot` 下。

---

## 2. 建议演示顺序

### 步骤 1：展示项目入口

建议先打开：

- `/root/EulerPilot/README.md`
- `/root/EulerPilot/docs/one_page_summary.md`

目的：

- 先让评委知道项目定位
- 先说明是本地系统 Agent，不是聊天 Agent

### 步骤 2：展示环境分工

建议说明：

- `192.168.1.121`
  - `SP3` 主交付环境
- `192.168.1.122`
  - `OLK-6.6` 正式 compare 环境

目的：

- 回答为什么同时有 `cgroup v2` 和 `sched_ext`

### 步骤 3：展示 Redis 候选结果

建议直接展示：

- `/root/EulerPilot/results/final/redis-scx-compare-20260612-191543`

重点看：

- `run_manifest.json`
- `compare_summary_avg.csv`
- `report.md`

### 步骤 4：展示 Nginx 候选结果

建议直接展示：

- `/root/EulerPilot/results/final/nginx-scx-compare-20260612-194018`

重点看：

- `run_manifest.json`
- `compare_summary_avg.csv`
- `report.md`

### 步骤 5：展示图表目录

建议直接展示：

- `/root/EulerPilot/reports/final_figures`

重点图包括：

- `redis_sched_ext_rps.svg`
- `redis_sched_ext_p99.svg`
- `nginx_sched_ext_rps.svg`
- `nginx_sched_ext_p99.svg`
- `psigate_timeline.svg`

### 步骤 6：展示最终总结文档

建议展示：

- `/root/EulerPilot/docs/final_results_summary.md`
- `/root/EulerPilot/docs/final_report_release_candidate.md`
- `/root/EulerPilot/docs/defense_summary.md`

---

## 3. 建议口径

演示时建议使用下面口径：

> EulerPilot 已经完成从 `SP3 + cgroup v2` 主闭环到 `OLK-6.6 + sched_ext` 正式 compare 的完整工程收口，当前已经形成 Redis 与 Nginx 两条业务线的 `RUNS=5` 候选结果目录。项目当前不再缺核心功能与正式实验，剩余工作主要是最终提交材料的整理与润色。

---

## 4. 当前最重要的文件

### 结果目录

- Redis：`/root/EulerPilot/results/final/redis-scx-compare-20260612-191543`
- Nginx：`/root/EulerPilot/results/final/nginx-scx-compare-20260612-194018`

### 图表目录

- `/root/EulerPilot/reports/final_figures`

### 文档目录

- `/root/EulerPilot/docs/final_report_release_candidate.md`
- `/root/EulerPilot/docs/final_results_summary.md`
- `/root/EulerPilot/docs/defense_summary.md`
- `/root/EulerPilot/docs/delivery_package_index.md`

---

## 5. 当前演示边界

演示时建议强调：

- 项目已经完成正式 compare
- 结果具备多轮候选目录
- `sched_ext` 结果需要按 workload 谨慎解释

不建议强调：

- `sched_ext` 在所有场景都优于默认调度器
- 某一组参数已经是绝对最优
