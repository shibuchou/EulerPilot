# EulerPilot 比赛最终完善路线图 v1.0

更新时间：2026-06-16

目标：在 **7 月 30 日** 前把项目从当前高完成度工程原型 (审计评分 **79/100**)，推进到高分竞争型作品。

---

## 0. 项目审计摘要

2026-06-16 对照比赛五项评分标准，在 192.168.1.121 上对项目进行了全面审计：

| 评分项 | 满分 | 得分 | 当前短板 |
|--------|------|------|----------|
| 创新性 | 30 | 23 | 分类方法朴素 (comm 匹配)、策略缺少自适应 |
| 功能完整性 | 25 | 22 | Network/Security 仅为 demo 级、sched_ext 环境耦合 |
| 性能提升 | 25 | 19 | sched_ext 部分模式有代价、单机规模、缺乏能耗数据 |
| 代码质量 | 10 | 7 | 源码几乎零注释、无单元测试、存在魔法数 |
| 演示效果 | 10 | 8 | 演示视频未制作 |
| **总计** | **100** | **79** | |

### 关键发现

- **架构双后端闭环完整**：SP3 cgroup v2 + OLK-6.6 sched_ext 都已经形成正式对照证据
- **实验证据链规范**：manifest 追溯、invalid_run 检测、多轮平衡轮换
- **文档体系成熟**：40 篇 MD 文档覆盖技术报告、答辩、操作手册
- **质量门禁通过**：12/12 PASS、安全审计 100% 通过
- **主要短板**：注释缺失、demo→产品化不足、SP4/K8s 未验证

### 约束原则

**121/122 已有交付快照不能被破坏**。所有增强在分支、配置开关、独立环境或 K8s lab namespace 中完成。

---

## 1. 最终目标定位

### 1.1 项目一句话

> EulerPilot 是一个面向 openEuler 的自适应资源管控 Agent：通过 eBPF/PSI 感知 workload，在用户态完成分类、策略决策和 Skills 编排，并通过 cgroup v2、sched_ext/scx、network/security eBPF hooks 等后端实现可观测、可回滚、可复现实验验证的系统级资源管控闭环。

### 1.2 最终交付形态

`	ext
EulerPilot v1.0
├── 主闭环：eBPF Observer -> Analyzer -> Policy Engine -> cgroup/scx Executor
├── Skills 平台：resource_control / psi_gate / network_policy / security_policy
├── 实验体系：Redis / Nginx / SP4 sched_ext / K8s lab 验证
├── 安全体系：默认关闭、fail-open、目标限定、rollback、审计日志
├── 工程体系：质量门禁、manifest 追溯、核心注释、测试、演示视频
└── 文档体系：技术报告、答辩 PPT、操作手册、K8s 验证报告、SP4 附录
`

### 1.3 后端定位

| 后端 | 定位 | 最终目标 |
|------|------|----------|
| cgroup v2 | SP3 主交付路径 | 必须稳定，作为兜底和正式主线 |
| sched_ext/scx | SP4/OLK 增强路径 | 官方环境复核，展示用户态调度创新 |
| network/security eBPF hooks | Skills 扩展路径 | 从 demo 变成可配置、可回滚、可限定目标的正式 Skill |

---

## 2. 环境规划

### 2.1 三环境结构

| 环境 | 角色 | 操作原则 |
|------|------|----------|
| 121 / SP3 | 主交付快照 | 保持稳定，只接受已验证修复和文档更新 |
| 122 / OLK-6.6 | sched_ext 已有验证环境 | 保留已有 RUNS=15 结果，继续调参但不覆盖主证据 |
| 123 / SP4 | 6 月 30 日后新增官方复核环境 | 新建，不升级 121；用于验证官方 SP4 sched_ext |

openEuler 24.03 LTS SP4 目前可以看到 dailybuild 目录，但 sched_ext 可用性以官方最终发布环境为准；报告中写 SP4 发布后复核，不提前写成已完成事实。

### 2.2 K8s 环境定位

核心目标：

`	ext
证明 EulerPilot 可以作为节点级 Agent/DaemonSet 部署；
只作用于 eulerpilot-lab namespace；
不影响 kube-system、master、现有业务 Pod；
Network/Security 策略能限定目标 Pod/cgroup，并可 rollback。
`

---

## 3. 分阶段规划

### 阶段 A：6 月 16 日–6 月 22 日 — 审计修复 + 基线加固

目标：修完审计发现的问题，补代码质量短板，形成 v0.2 稳定基线。

#### A1. 生命周期和审计修复

1. NetworkPolicySkill::stop() 调 rollback()，清理 BPF link/cgroup
2. main.cpp 异常路径 stop_all() 确保执行
3. ollback.sh 支持 EULERPILOT_SCX_BINARY
4. manifest 增加 git_commit / git_tag / config_sha / source_sha
5. inal_results_summary.md 同步 RUNS=15
6. README 说明默认 gate_mode=always-active，PsiGate 需显式启用

验收：

`ash
make -B agent observer
./build/eulerpilot-agent --list-skills
./build/eulerpilot-agent --doctor-skills
timeout 5s ./build/eulerpilot-agent --config configs/agent.yaml
bash scripts/final_quality_gate.sh --quick
bash scripts/rollback.sh && bash scripts/rollback.sh   # 幂等
`

#### A2. 核心注释补齐

只补设计意图注释，不逐行注释：

| 文件 | 函数/模块 | 注释重点 |
|------|-----------|----------|
| untime.cpp | classify_sample | comm 匹配 + 指标阈值为何这样设计 |
| untime.cpp | uild_trigger_context | 场景证据、压力证据、控制级别三层逻辑 |
| untime.cpp | derive_desired_mode | PsiGate 状态迁移和防抖 |
| psi_gate.cpp | 状态机 | NORMAL→ARMED→ACTIVE→COOLDOWN 语义 |
| executors.cpp | cgroup/scx 执行 | fallback、session 管理、class_map |
| uiltin_skills.cpp | stop/rollback | 生命周期清理和幂等要求 |
| scx_eulerpilot.bpf.c | enqueue/dispatch | DSQ 分流、shared fallback、饥饿保护 |

#### A3. 魔法数收口

轻量收口为具名常量 + 报告关键参数表：

| 参数 | 默认值 | 含义 | 来源 |
|------|--------|------|------|
| latency wait threshold | 5ms | 延迟任务等待放大判断 | 经验阈值 + 扫描 |
| background runtime threshold | 4ms | 后台任务干扰判断 | 经验阈值 + 扫描 |
| psi threshold | config/env | 系统压力窗口判断 | 实验扫描 |
| cooldown cycles | config/env | 防止 gate 抖动 | 状态机稳定性 |

#### A4. 第一版演示视频

6–8 分钟：项目定位 → skills 展示 → Agent smoke → Redis RUNS=15 Dashboard → Nginx 策略边界 → network/security deny→rollback→recover → quality gate 12/12 → 双环境 + SP4 计划

---

### 阶段 B：6 月 23 日–6 月 29 日 — Network/Security 成品化

目标：Network/Security 从 demo 升级为可配置、可回滚的正式 Skills。

#### B1. NetworkPolicySkill

配置格式新增/扩展：

`yaml
network_policy:
  enabled: false
  mode: enforce        # dry-run | enforce
  target:
    type: cgroup       # cgroup | pid | k8s_pod
    cgroup_path: 
 rules:
 - name: deny_redis_port
 protocol: tcp
 dst_port: 6379
 action: deny
 default_action: allow
 audit:
 enabled: true
 path: reports/network_policy_events.jsonl
 cleanup:
 auto_detach_on_stop: true
 unpin_on_rollback: true
`

必须能力：默认 disabled、dry-run/enforce 双模式、单端口 deny、target cgroup 限定、事件日志、stop 自动 detach、rollback 幂等、doctor 检测。

#### B2. SecurityPolicySkill

`yaml
security_policy:
 enabled: false
 mode: enforce
 target:
 type: cgroup
 rules:
 - name: deny_core_pattern_write
 hook: file_open
 path: /proc/sys/kernel/core_pattern
 permissions: write
 action: deny
 default_action: allow
 audit:
 enabled: true
 path: reports/security_policy_events.jsonl
 cleanup:
 auto_detach_on_stop: true
 unpin_on_rollback: true
`

安全原则：BPF LSM hook 全局触发，BPF 程序先做 ask_in_target_cgroup() 过滤；默认 allow；默认 disabled；仅命中目标 workload 才 deny；dry-run 不拦截只记录；rollback 后完全恢复。

#### B3. 验收

`ash
bash scripts/demo_network_policy_product.sh --dry-run
bash scripts/demo_network_policy_product.sh --enforce
bash scripts/demo_security_policy_product.sh --dry-run
bash scripts/demo_security_policy_product.sh --enforce
bash scripts/rollback.sh
bash scripts/final_quality_gate.sh --quick
`

---

### 阶段 C：6 月 30 日–7 月 10 日 — SP4 官方环境复核

目标：在 openEuler 24.03 LTS SP4 官方环境上完成 sched_ext 复核。

#### C1. 新建 123（不升级 121）

#### C2. SP4 核验步骤

能力检测 → 编译 → smoke → 小正式 → 最终复核：

`ash
# 能力检测
grep SCHED_CLASS_EXT /boot/config-
test -d /sys/kernel/sched_ext && echo sched_ext available

# 编译
cd /root/EulerPilot && make clean && make -B agent observer

# smoke
RUNS=1 bash bench/redis/run_redis_sched_ext_compare.sh

# 正式
RUNS=5 bash bench/redis/run_redis_sched_ext_compare.sh
RUNS=15 bash bench/redis/run_redis_sched_ext_compare.sh # 稳定后
`

#### C3. SP4 最终产物

- docs/sp4_sched_ext_validation.md
- esults/final/sp4-redis-scx-compare-YYYYMMDD-HHMMSS/
- 技术报告 SP4 附录

报告口径：

> 若 SP4 支持 sched_ext： sched_ext 已在 openEuler 24.03 LTS SP4 官方环境完成复核。
>
> 若不支持或不稳定： 项目保留 SP3 cgroup v2 主交付与 OLK-6.6 sched_ext 增强验证；SP4 复核记录作为后续兼容性工作。

---

### 阶段 D：7 月 11 日–7 月 20 日 — K8s 非侵入验证

目标：证明 EulerPilot 支持多节点部署，且不影响集群正常运行。

#### D1. 隔离方案

`ash
kubectl create ns eulerpilot-lab
kubectl label node k8s-worker1 eulerpilot.io/lab=true
`

实验 Pod 使用 nodeSelector + namespace 限定，不影响 kube-system 和现有业务。

#### D2. DaemonSet 设计

新增 deploy/k8s/ 目录：
amespace.yaml、configmap.yaml、daemonset.yaml、bac.yaml、lab Pod YAML。

原则：只调度到 worker、默认 observe-only、需要 hostPID 时在文档解释原因。

#### D3. Network/Security K8s 验证

- **Network**: Pod net-demo 禁止访问 redis:6379，enable 前成功、enforce 后失败、rollback 后恢复
- **Security**: Pod sec-demo 禁止写 /proc/sys/kernel/core_pattern，BPF 程序默认 allow 仅实验 cgroup deny

#### D4. K8s 产物

- docs/k8s_validation.md
- deploy/k8s/*.yaml
- demo/k8s_demo_runbook.md

---

### 阶段 E：7 月 21 日–7 月 30 日 — 最终冻结与收口

目标：冻结代码、完成全部交付材料、打 release tag。

#### E1. 功能冻结

7 月 21 日后只修 P0/P1 bug，不再加功能。冻结：代码、配置、实验结果、Dashboard、报告、PPT、视频、README、release tag。

#### E2. 最终质量门禁

`ash
make clean && make -B agent observer
./scripts/check_env.sh
./build/eulerpilot-agent --list-skills
./build/eulerpilot-agent --doctor-skills
timeout 15s ./build/eulerpilot-agent --config configs/agent.yaml
bash scripts/final_quality_gate.sh
bash scripts/rollback.sh
`

122/123 视情况跑：./build/eulerpilot-agent --doctor-skills + RUNS=1 bash bench/redis/run_redis_sched_ext_compare.sh

#### E3. 最终技术报告结构

` ext
1. 项目背景与赛题要求
2. 系统总体设计
3. eBPF Observer 与 workload 感知
4. Policy Engine 与 PsiGate
5. Skills 框架设计
6. cgroup v2 主执行路径
7. sched_ext/scx 增强路径
8. Network/Security Skills 成品化
9. 实验设计与可复现性
10. Redis RUNS=15 结果
11. Nginx 策略边界分析
12. SP4 官方环境复核
13. K8s 非侵入部署验证
14. 工程质量与安全审计
15. 局限性与后续工作
`

#### E4. 最终 PPT 结构

` ext
1. 赛题痛点：混部干扰下关键服务尾延迟
2. EulerPilot 总体架构
3. eBPF/PSI 观测与 workload 分类
4. Policy Engine + PsiGate
5. cgroup v2 主闭环
6. sched_ext/scx 增强路径
7. Skills 框架：resource/network/security
8. Redis RUNS=15 核心结果
9. Nginx 边界与自适应必要性
10. SP4 + K8s 验证
11. 工程质量：12/12 gate、rollback、安全审计
12. 总结与创新点
`

---

## 4. 关键不足与解决方式

| 不足 | 是否赛前解决 | 解决方式 |
|------|-------------|----------|
| 源码注释少 | **必须** | 核心函数补设计意图注释 |
| 演示视频未制作 | **必须** | 6–8 分钟视频，展示完整闭环 |
| 无单元测试 | 建议 | 至少加 decision/policy 自测或 smoke test 文档 |
| 魔法数散落 | **必须** | 常量命名 + 参数表，不大改 YAML |
| sched_ext 环境耦合 | **必须说明** | 121/122/123 三环境定位 + SP4 附录 |
| Network/Security demo 未成品化 | **必须增强** | YAML rules、target、dry-run、rollback、audit |
| K8s 未验证 | 建议增强 | eulerpilot-lab namespace 非侵入验证 |
| NUMA 未验证 | 可选 | 仅当真实多 NUMA 环境时做 |
| 横向对比缺失 | 可选 | 文档简单对比 ghOSt/scx examples |
| manifest 追溯不足 | **必须** | 增加 git/config/source hash |
| Git 仓库态不干净 | **必须** | 清理 build、pycache、bak；补 .gitignore；真实 commit |
| Nginx sched_ext 负向结果 | **必须处理** | 策略边界分析，不包装成成功 |

---

## 5. NUMA、多机、跨版本取舍

- **多机**：用 K8s 验证节点级部署和策略分发，是加分项
- **NUMA**：有 2+ NUMA node 才做，否则跳过
- **跨 Linux 版本**：SP4 官方 > K8s 多节点 > NUMA > Ubuntu/Fedora

---

## 6. 红线与注意事项

- **不破坏主交付快照**：121 不升级 SP4、122 保留 OLK-6.6 结果、SP4 新建 123
- **不影响 K8s 正常运行**：仅 eulerpilot-lab namespace、默认 observe-only/disabled、security BPF 默认 allow、不碰 kube-system/master
- **不过度扩大范围**：不做 K8s Operator/CRD、不做 ML 分类器、不做完整网络/安全策略系统

---

## 7. 里程碑总览

| 里程碑 | 日期 | 验收标准 |
|--------|------|----------|
| **M1** | 6 月 22 日 | 审计修复完成、核心注释完成、演示视频 v1、quality gate 通过 |
| **M2** | 6 月 29 日 | Network/Security 产品化、dry-run/enforce/rollback/audit、demo 文档 |
| **M3** | 7 月 10 日 | SP4 复核 RUNS≥5、SP4 附录 |
| **M4** | 7 月 20 日 | K8s lab 验证、DaemonSet/ConfigMap、不影响集群证明 |
| **M5** | 7 月 30 日 | 最终报告/PPT/视频/Dashboard/GitHub tag/提交包/答辩讲稿 |

---

## 8. 预期评分提升

| 评分项 | 当前 | 目标 | 提升要点 |
|--------|------|------|----------|
| 创新性 | 23/30 | 25-27 | SP4 复核 + PsiGate + Skills 平台化 |
| 功能完整性 | 22/25 | 24-25 | 产品化 Skill + K8s 验证 |
| 性能提升 | 19/25 | 21-23 | Redis RUNS=15 + SP4 复核 + 边界分析 |
| 代码质量 | 7/10 | 8-9 | 核心注释 + 自测 + manifest 追溯 |
| 演示效果 | 8/10 | 9-10 | 完整视频 + K8s live/demo + Dashboard |
| **总计** | **79** | **88-91** | |

---

## 9. 立即执行清单

` ext
1. 完成审计修复（阶段 A1）
2. 补核心注释（阶段 A2）
3. 清理仓库态：.gitignore、build、pycache、bak
4. 录演示视频 v1（阶段 A4）
5. 设计 network_policy/security_policy YAML schema（阶段 B1/B2）
6. 实现 target cgroup + dry-run + audit（阶段 B1/B2）
7. 实现 network/security rollback 幂等验证（阶段 B3）
8. 写 docs/network_security_skills.md
9. 等 SP4 环境发布后新建 123（阶段 C）
10. 做 K8s eulerpilot-lab namespace 验证（阶段 D）
`

---

一句话总结：

> 先把当前项目变稳，再把 demo 变成正式 Skill；等 SP4 出来做官方 sched_ext 复核；最后用 K8s 非侵入验证证明平台化和多节点能力。
