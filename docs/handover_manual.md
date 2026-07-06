# EulerPilot 交接手册

更新时间：`2026-06-14`

## 1. 这份文档是给谁看的

这份文档是给**刚接手 EulerPilot 项目的人**看的。

目标不是解释某一个文件，而是回答下面这些问题：

1. 项目现在到底做到哪了？
2. 哪些文档是主入口，哪些只是历史材料？
3. 两台机器各自干什么？
4. 现在有哪些正式候选结果目录？
5. 代码主线在哪？
6. 当前还在做什么？
7. 如果你今天接手，应该按什么顺序继续做？

---

## 2. 一句话状态

当前项目已经不是“功能开发中”，而是：

> **主线能力、双后端正式实验、Redis/Nginx 候选结果、图表和中文主稿都已经具备，当前进入最终 release candidate 定稿阶段。**

更具体地说：

- `SP3 + cgroup v2` 主闭环已经完成
- `OLK-6.6 + sched_ext` 正式 compare 已经完成
- Redis `sched_ext` 已有 `RUNS=5` 正式候选结果
- Nginx `sched_ext` 已有 `RUNS=5` 正式候选结果
- 图表材料已经生成
- 中文最终报告主稿链已经基本齐全

当前剩余工作已经主要收敛为：

- 最终报告语言润色
- 图表插入与排版
- 答辩页整理
- 以及 `skills / YAML` 这一轮新开发的继续实现与回归验证

---

## 3. 两台机器的最终分工

### 3.1 `192.168.1.121`

```text
角色：官方 SP3 主交付机 / 默认开发机
系统：openEuler 24.03 LTS SP3
项目目录：/root/EulerPilot
```

当前这台机器负责：

- 默认代码开发
- `cgroup v2` 主线
- `skills / YAML` 新一轮改造
- 文档与报告主入口维护

当前这台机器**不负责**：

- `sched_ext` 正式候选实验
- 覆盖 Redis/Nginx 已冻结的最终结果目录

### 3.2 `192.168.1.122`

```text
角色：OLK-6.6 sched_ext 验证机
hostname：cernet2.net
内核：6.6.0-olk66-scx
项目目录：/root/EulerPilot
```

当前这台机器负责：

- `sched_ext` 正式 compare
- `PsiGate` 回归验证
- 最终候选结果目录
- 最终图表目录

当前这台机器**不应作为日常开发沙箱**，原因是：

- 它已经承担正式候选结果和图表产出角色
- 不应在这里继续随意改主线实现
- 只应做技能改造同步后的最小回归验证

---

## 4. 当前最重要的正式结果目录

### 4.1 Redis

当前最重要的 Redis 正式候选结果目录是：

- `/root/EulerPilot/results/final/redis-scx-compare-20260612-191543`

说明：

- 这是**远端验证机 `192.168.1.122`** 上的路径
- 当前已满足：
  - `RUNS=5`
  - `run_manifest.json`
  - `compare_summary_avg.csv`
  - `report.md`
  - `summary.md`
  - 无 `invalid_run`

早期结果目录仍保留，但不建议作为正文主引用：

- `185007`
- `185727`

### 4.2 Nginx

当前最重要的 Nginx 正式候选结果目录是：

- `/root/EulerPilot/results/final/nginx-scx-compare-20260612-194018`

说明：

- 同样是**远端验证机 `192.168.1.122`** 上的路径
- 当前已满足：
  - `RUNS=5`
  - `run_manifest.json`
  - `compare_summary_avg.csv`
  - `report.md`
  - `summary.md`
  - 无 `invalid_run`

早期目录也保留：

- `191150`
- `193017`

但当前正文建议优先引用 `194018`。

---

## 5. 当前最重要的图表目录

图表当前统一位于：

- `/root/EulerPilot/reports/final_figures`

说明：

- 这同样是**远端验证机 `192.168.1.122`** 上的路径

当前已生成：

- `redis_sched_ext_rps.svg`
- `redis_sched_ext_p99.svg`
- `nginx_sched_ext_rps.svg`
- `nginx_sched_ext_p99.svg`
- `redis_quiet_overhead.svg`
- `nginx_quiet_overhead.svg`
- `psigate_timeline.svg`

---

## 6. 当前最重要的文档入口

如果你是刚接手的人，文档不要从头乱翻。按下面顺序看：

### 第 1 层：总入口

- `/root/EulerPilot/README.md`
- `/root/EulerPilot/bench/README.md`

这两个文件回答：

- 项目是什么
- 当前到哪了
- 最重要的实验入口和结果目录在哪里

### 第 2 层：当前状态总览

- `/root/EulerPilot/docs/project_status_overview.md`
- `/root/EulerPilot/docs/stage_delivery_summary.md`
- `/root/EulerPilot/docs/final_delivery_status.md`

这三份主要回答：

- 当前项目做到哪了
- 哪些已经完成
- 当前还剩什么

### 第 3 层：结果与交付

- `/root/EulerPilot/docs/final_results_summary.md`
- `/root/EulerPilot/docs/final_delivery_audit.md`
- `/root/EulerPilot/docs/submission_checklist.md`
- `/root/EulerPilot/docs/delivery_package_index.md`
- `/root/EulerPilot/docs/final_submission_readme.md`
- `/root/EulerPilot/docs/final_submission_packet.md`

这几份主要回答：

- 最终候选结果目录是什么
- 图表有哪些
- 当前可交付边界是什么
- 哪些事情还只剩人工润色

### 第 4 层：正式报告与答辩材料

#### 当前建议主稿

- `/root/EulerPilot/docs/final_report_submission.md`

#### 其他版本

- `/root/EulerPilot/docs/final_report_release_candidate.md`
- `/root/EulerPilot/docs/final_report_v2.md`
- `/root/EulerPilot/docs/final_report_v1.md`
- `/root/EulerPilot/docs/final_report_draft.md`
- `/root/EulerPilot/docs/final_report_outline.md`

注意：

- 这些文件都是同一条文稿链的不同阶段版本
- 当前建议**继续润色并作为最终提交主稿使用**的是：
  - `/root/EulerPilot/docs/final_report_submission.md`

#### 答辩与演示材料

- `/root/EulerPilot/docs/defense_summary.md`
- `/root/EulerPilot/docs/defense_slides_outline.md`
- `/root/EulerPilot/docs/final_talk_script.md`
- `/root/EulerPilot/docs/demo_runbook.md`
- `/root/EulerPilot/docs/one_page_summary.md`
- `/root/EulerPilot/demo/README.md`

---

## 7. `reports/` 目录怎么理解

`reports/` 目录里当前有：

- `technical_report_draft.md`
- `technical_report_v1.md`
- `technical_report_v2.md`
- `technical_report_v3.md`

这些文件现在都已经被我收成“桥接说明”，不要再把它们当真正主稿继续扩写。

如果你要继续写最终报告，直接用：

- `/root/EulerPilot/docs/final_report_submission.md`

如果你要给别人一个报告入口，可以给：

- `/root/EulerPilot/reports/technical_report_v3.md`

---

## 8. 代码主线怎么看

如果你要快速看代码，推荐顺序是：

1. `agent/src/main.cpp`
2. `agent/src/runtime.cpp`
3. `agent/src/executors.cpp`
4. `agent/src/psi_gate.cpp`
5. `sched/scx_eulerpilot.bpf.c`
6. `bench/redis/`
7. `bench/nginx/`

### 当前关键代码目录

#### Agent 主体

- `agent/include/eulerpilot.hpp`
- `agent/src/main.cpp`
- `agent/src/runtime.cpp`
- `agent/src/executors.cpp`
- `agent/src/psi_gate.cpp`

#### Skills / YAML 改造相关

- `agent/include/skill.hpp`
- `agent/include/skill_registry.hpp`
- `agent/include/skill_manager.hpp`
- `agent/include/builtin_skills.hpp`
- `agent/include/skill_runtime_context.hpp`
- `agent/src/skill_registry.cpp`
- `agent/src/skill_manager.cpp`
- `agent/src/builtin_skills.cpp`
- `agent/src/skill_runtime_context.cpp`

#### 配置

- `configs/agent.yaml`
- `configs/policy.yaml`
- `configs/psi_gate.yaml`
- `configs/skills.yaml`

#### BPF / sched_ext

- `bpf/workload_observer.bpf.c`
- `bpf/network_policy.bpf.c`
- `sched/scx_eulerpilot.bpf.c`
- `sched/scx_eulerpilot.c`

---

## 9. 当前实验脚本怎么分层

### Redis 主线

- `bench/redis/run_redis_main_experiment.sh`
- `bench/redis/run_redis_final_experiment.sh`
- `bench/redis/run_redis_sched_ext_compare.sh`

参数扫描脚本：

- `run_profile_sweep.sh`
- `run_psi_threshold_sweep.sh`
- `run_trigger_sweep.sh`
- `run_trigger_sweep_local_refine.sh`
- `run_trigger_sweep_incr_refine.sh`
- `run_background_weight_refine.sh`

### Nginx 第二线

- `bench/nginx/run_nginx_main_experiment.sh`
- `bench/nginx/run_nginx_sched_ext_compare.sh`

### PsiGate smoke

- `bench/psi/run_loader_wiring_smoke.sh`
- `bench/psi/run_gate_mode_smoke.sh`
- `bench/psi/run_psi_agent_smoke.sh`

当前状态：

- 这些脚本都已经存在
- Redis/Nginx 正式 compare 已形成候选结果目录
- 新接手者**不要再重铺实验矩阵**，除非真的发现结果不可复现或结论性错误

---

## 10. 当前 `skills / YAML` 改造做到哪了

这一轮是你接手后最可能继续动手的地方。

当前已经完成：

- `yaml-cpp` 依赖已安装
- `Skill / SkillRegistry / SkillManager / builtin_skills` 基础层已落地
- `configs/agent.yaml` 增加了 `skills_config_path`
- `configs/skills.yaml` 已切到 `schema_version: 1`
- `--list-skills` 已可用
- `--doctor-skills` 已可用
- `make agent` 已通过
- `make network-policy` 已通过

当前实测：

- `--list-skills`
  - 能输出：
    - `resource_control`
    - `psi_gate`
    - `network_policy_demo`
- `--doctor-skills`
  - `resource_control` available
  - `psi_gate` available
  - `network_policy_demo` unavailable，原因是：
    - `cgroup-root-not-writable`

也就是说，`skills / YAML` 这一轮已经从“只有规划文档”推进到了：

> **可编译、可列出、可探测的最小闭环阶段**

但还没有完全做完。

### 当前还没做完的点

1. `SkillManager` 还没有完全成为唯一生命周期所有者  
   现在 runtime 已经开始让 skill 持有共享上下文，但旧直接执行路径还没有完全退位。

2. `resource_control / psi_gate` 还是轻量 adapter  
   已能探测和输出 snapshot，但还没彻底接管原有主循环里的生命周期语义。

3. `network_policy_demo` 还没跑最小 smoke  
   现在已有：
   - BPF 程序
   - 独立构建目标
   - cleanup 脚本
   但还没走通：
   - `curl -4`
   - attach / deny / rollback / recover

4. `122` 上最小回归还没开始  
   目前我已确认：
   - `122` 上还没有 `/root/EulerPilot-skills` 独立工作区

### 当前继续做这轮改造时的优先顺序

继续时不要乱跳，建议顺序固定为：

1. 收拢 `run_cycles()` 与 `SkillManager` 的生命周期所有权
2. 把 `resource_control / psi_gate` adapter 从“探测级”推进到“运行期持有级”
3. 把 `network_policy_demo` 做成最小 smoke 闭环
4. 在 `122` 上创建独立工作区
5. 只做最小回归：
   - `make agent`
   - `--list-skills`
   - `--doctor-skills`
   - `sched_ext attach/detach`
   - `PsiGate` wiring smoke
   - Redis `sched_ext` smoke
   - `nr_rejected=0`
   - detach 后 `state=disabled`

注意：

- `122` 只做回归
- 不要在 `122` 上重跑 `RUNS=5`
- 不要覆盖现有正式结果目录

---

## 11. 当前什么不能乱动

你接手之后，下面这些东西不要随便动：

### 正式候选结果目录

- `/root/EulerPilot/results/final/redis-scx-compare-20260612-191543`
- `/root/EulerPilot/results/final/nginx-scx-compare-20260612-194018`

### 图表目录

- `/root/EulerPilot/reports/final_figures`

### 正文主稿建议入口

- `/root/EulerPilot/docs/final_report_submission.md`

### 旧报告桥接文件

- `reports/technical_report_draft.md`
- `reports/technical_report_v1.md`
- `reports/technical_report_v2.md`
- `reports/technical_report_v3.md`

它们现在是入口说明，不要再拿来继续扩写内容。

---

## 12. 如果你今天就要继续干，最合理的顺序

### 路线 A：继续 `skills / YAML`

如果你是继续实现的人，顺序就是：

1. 看：
   - `/root/EulerPilot/docs/skills_yaml_plan.md`
   - `/root/EulerPilot/agent/src/skill_manager.cpp`
   - `/root/EulerPilot/agent/src/builtin_skills.cpp`
2. 先在 `121` 完成：
   - lifecycle 所有权收拢
   - `network_policy_demo` 最小 smoke
3. 再到 `122` 做：
   - 最小回归

### 路线 B：继续最终提交

如果你是继续交付材料的人，顺序就是：

1. 先读：
   - `/root/EulerPilot/docs/final_report_submission.md`
   - `/root/EulerPilot/docs/final_results_summary.md`
   - `/root/EulerPilot/docs/final_delivery_audit.md`
2. 再看图：
   - `/root/EulerPilot/reports/final_figures`
3. 再准备：
   - `/root/EulerPilot/docs/defense_slides_outline.md`
   - `/root/EulerPilot/docs/final_talk_script.md`

---

## 13. 当前最严格的状态结论

当前项目已经：

- 完成主功能闭环
- 完成双后端正式实验
- 拿到 Redis / Nginx 两条业务线的 `RUNS=5` 候选结果
- 生成图表材料
- 形成中文主稿和答辩材料入口

当前剩余工作：

- 如果做交付：只剩润色、排版、PPT
- 如果做 `skills / YAML`：只剩 lifecycle 收拢、network demo smoke 和 122 最小回归

所以可以直接告诉新接手的人：

> 项目主线已经完成，候选结果和文档主链都已经成型。现在不是从零开始，而是在一个已经基本收口的项目上做最后两类工作：一类是 `skills / YAML` 最小闭环实现，一类是最终提交材料定稿。
