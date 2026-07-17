# EulerPilot 最终答辩文档

更新时间：`2026-07-08`

---

## 一、作品概述

**EulerPilot** 是一个面向 openEuler 的自适应资源管控 Agent。

它不是聊天型 AI Agent，而是一个本地运行的系统自治控制程序：

```
eBPF 观测 -> Workload 分类 -> 策略决策 -> Skills 编排 -> 双后端执行 -> 实验验证
```

---

## 二、核心架构

```
Observer (eBPF + PSI)
  -> Analyzer (workload 识别)
  -> Policy Engine (三层分层证据)
  -> Skill Manager (Resource / Network / Security / Policy Engine + YAML 驱动)
  -> Executor (CgroupExecutor / ScxExecutor)
  -> Benchmark / Report (Redis + Nginx 双业务线)
```

### 模块明细

| 模块 | 功能 | 状态 |
|------|------|------|
| Observer | eBPF 采集 sched_wakeup/switch/migrate + PSI | ✅ |
| Analyzer | 识别 redis-server / nginx / stress-ng / make / sysbench | ✅ |
| Policy Engine | 三层分层证据：场景前提 -> 压力证据 -> 控制分级 | ✅ |
| Skill Manager | Resource / Network / Security / Policy Engine + YAML 驱动 + 拓扑排序 + 倒序回滚 | ✅ |
| CgroupExecutor | cpu.weight + cgroup.procs（SP3 主线） | ✅ |
| ScxExecutor | class_map -> DSQ 分流（OLK-6.6 / SP4 增强线） | ✅ |
| PsiGate v1 | NORMAL -> ARMED -> ACTIVE -> COOLDOWN 状态机 | ✅ |
| Benchmark | 多后端矩阵 + 多轮运行 + 平衡轮换 + 中文报告 | ✅ |

---

## 三、赛题覆盖：OS Agent 三方向

| 方向 | 实现方式 | 等级 |
|------|----------|------|
| **resource control agent** | CPU/Memory/IO 自动闭环，cgroup/scx 双后端，真实 container / k3s Pod target | 主线 |
| **network policy agent** | connect4、TC QoS、XDP、协议/IP/端口 tuple、真实 Pod host veth | 已完成 |
| **security policy agent** | BPF LSM、syscall tracing、服务联动 anomaly、credential anomaly/deep hook 评估 | 已完成 |
| **policy engine agent** | Security anomaly -> Resource / Network+Resource / real Pod 联动，事务化回滚 | 已完成 |

---

## 四、核心创新点

### 1. 双后端统一 Agent 架构
同一套 Observer/Analyzer/Policy Engine，不同执行后端切换。兼顾 SP3 稳定交付与 OLK-6.6 sched_ext 创新验证。

### 2. PsiGate v1 分层门控
状态机 NORMAL -> ARMED -> ACTIVE -> COOLDOWN。PSI 作为压力窗口信号参与决策，不是业务退化标签。

### 3. Skills 插件化框架
`Skill -> SkillRegistry -> SkillManager -> builtin_skills` 四层结构。
- `skills.yaml` 驱动启停配置
- `--list-skills` / `--doctor-skills` 命令行验证
- 新增 Skill：写 BPF 程序 + Skill 适配器 + YAML 配置，不侵入 core Runtime

### 4. 正式 compare 实验框架
- 7 种后端组合矩阵
- RUNS=5 多轮运行
- 平衡轮换顺序
- `run_manifest.json` 记录实验上下文
- `invalid_run` 机制
- 自动生成中文报告

---

## 五、实验结果

### Redis + stress-ng

| 项目 | 内容 |
|------|------|
| 候选目录 | `results/final/redis-scx-compare-20260612-191543` |
| SP4 复核目录 | `results/final/redis-scx-compare-20260708-150702` |
| 轮数 | RUNS=5 |
| 矩阵 | quiet_default / quiet_scx_normal / noisy_default / noisy_cgroup_v2 / noisy_scx_normal / noisy_scx_always_active / noisy_scx_psi |
| 观察 | noisy_cgroup_v2 在 GET 上吞吐明显提升；noisy_scx_normal 在 GET/INCR/SET 上 RPS 改善 |
| 图表 | redis_sched_ext_rps.svg / redis_sched_ext_p99.svg / redis_quiet_overhead.svg |

### Nginx + stress-ng

| 项目 | 内容 |
|------|------|
| 候选目录 | `results/final/nginx-scx-compare-20260612-194018` |
| SP4 复核目录 | `results/final/nginx-scx-compare-20260708-152602` |
| 轮数 | RUNS=5 |
| 矩阵 | 与 Redis 保持一致 |
| 观察 | cgroup_v2 在 Nginx 场景下更稳；部分 sched_ext 模式存在明显 P99 代价 |
| 图表 | nginx_sched_ext_rps.svg / nginx_sched_ext_p99.svg / nginx_quiet_overhead.svg |

### 关键证据链

```
latency + background 场景前提
  -> PsiGate 进入 ACTIVE
  -> cgroup_v2: applied=yes reason=assigned
  -> sched_ext: executor=sched_ext
  -> 业务结果写入正式候选目录
```

图表：`psigate_timeline.svg`

---

## 六、Skills 演示：现场可跑的命令

### 查看所有 Skills
```bash
./build/eulerpilot-agent --list-skills
# 输出包含：resource_control, psi_gate, network_policy/network_qos/network_xdp, security_policy, policy_engine 等能力
```

### 探测 Skills 状态
```bash
./build/eulerpilot-agent --doctor-skills --config configs/agent.yaml
# 全部 available=yes，默认 network/security demo 为 disabled
```

### Policy Engine 主演示链路
```bash
bash tests/integration/test_policy_engine_security_network_resource.sh
# security_policy burst_connect
# -> policy_engine decision
# -> resource_control cpu.max / memory.high
# -> network_qos tc/tbf 2mbit
# -> rollback
```

### network_policy_demo 兼容回归入口
```bash
python3 -m http.server 18080 --bind 127.0.0.1 &
# 临时启用 network_policy_demo，启动 Agent
# cgroup 内 curl -> 000（deny），cgroup 外 -> 200（allow）
# Agent 退出 -> 恢复
```

### security_policy_demo 兼容回归入口
```bash
cat demo/security_policy_demo/secret.txt  # -> "TOP SECRET"
# 临时启用 security_policy_demo，启动 Agent
cat demo/security_policy_demo/secret.txt  # -> Operation not permitted
# Agent 退出 -> 恢复
```

---

## 七、环境清单

| 环境 | IP | 系统 | 角色 |
|------|-----|------|------|
| SP4 主验证机 | 192.168.1.123 | openEuler 24.03 LTS SP4，自编译 sched_ext 内核 | 核心验证仓库、最终 evidence、Web Console、K8s 旁路验证、质量门禁 |
| SP3 历史验证机 | 192.168.1.121 | openEuler 24.03 LTS SP3 | 初代 cgroup v2 主闭环、历史结果和回归对照 |
| OLK 对照验证机 | 192.168.1.122 | openEuler 24.03 LTS SP3 (6.6.0-olk66-scx) | sched_ext 正式对照和双机历史结果 |
| GitHub | `shibuchou/EulerPilot` | — | 代码仓库（私密） |

---

## 八、交付物清单

| 类别 | 内容 |
|------|------|
| 核心代码 | agent/src, bpf/, sched/, configs/, scripts/ |
| 最终报告 | `docs/final_report_submission.md`（375 行） |
| 结果摘要 | `docs/final_results_summary.md` |
| 答辩摘要 | `docs/defense_summary.md` |
| 答辩提纲 | `docs/defense_slides_outline.md` |
| 答辩讲稿 | `docs/final_talk_script.md` |
| 演示说明 | `docs/demo_runbook.md` |
| 交接手册 | `docs/handover_manual.md`（502 行） |
| 提交清单 | `docs/submission_checklist.md` |
| 可视化 | `reports/dashboard/index.html` 静态 Dashboard + Agent `/metrics` 端点 + `web_console/` |
| 图表 | `reports/final_figures/`（7 张 SVG） |
| 实验结果 | `results/final/`、`results/network_policy/`、`results/security_policy/`、`results/resource_control/`、`results/policy_engine/` |
| 证据压缩入口 | `configs/final_evidence_manifest.json`、`reports/final_evidence_compact.md/json`，37 条核心证据、必需缺失 0、警告 0 |
| 代码仓库 | `https://github.com/shibuchou/EulerPilot` |

---

## 九、结论边界

**可以说的：**
- 系统实现完成，双后端均可真实运行
- Redis/Nginx 双业务线均有 RUNS=5 正式候选结果，SP4 上有 RUNS=5 复核结果
- SP4 上补充 Redis pressure gradient 与 manual static vs agent dynamic 对比，用于解释干扰强度变化、CPU 效率和动态调控价值
- Skills 框架可扩展，Network/Security/Resource/Policy Engine 证明 eBPF hook 与系统控制器可统一编排
- 正式 compare 框架具备可复现性

**不能说的：**
- sched_ext 对所有 workload 都稳定提升
- 所有场景都稳定带来收益
- 某一组参数已经是绝对最优

---

## 十、最终结论

> EulerPilot 已完成为一个面向 openEuler 的、可运行、可实验、可解释、可复现、可扩展的系统资源管控 Agent 工程闭环。项目同时覆盖了 resource control、network policy、security policy 三个 OS Agent 扩展方向，并通过 Policy Engine 跨 Skill 联动、SP4 发行环境适配、SP4 官方源码自编译 sched_ext 内核复核、Kubernetes 旁路验证、Web Console 和 37 条 final evidence compact 证明了新增 eBPF hook 与系统控制器可以在统一 Agent 框架下安全编排和回滚。

**当前状态：代码冻结，已进入提交准备阶段。**

