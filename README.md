# EulerPilot

面向 openEuler 的自适应资源管控 Agent — 一个可运行、可实验、可扩展的系统自治控制程序。

## 它解决什么问题

服务器上同时存在 Redis/Nginx 等延迟敏感服务与 stress-ng 等后台干扰任务时，如何自动感知 workload、做出可解释的调控决策、并输出可复现的性能结果？

EulerPilot 的答案是：**eBPF 观测 → 用户态分类 → 分层决策 → 双后端执行 → 实验验证**。

## 架构

```
┌─────────────────────────────────────────────────────────┐
│                      EulerPilot                          │
│                                                          │
│  bpf/                     agent/                         │
│  ┌──────────┐            ┌──────────────┐               │
│  │ Observer │──metrics──→│   Analyzer   │               │
│  │ (eBPF)   │            │  (workload   │               │
│  │ wakeup   │            │  classifier) │               │
│  │ switch   │            └──────┬───────┘               │
│  │ migrate  │                   ↓                       │
│  │ + PSI    │            ┌──────────────┐               │
│  └──────────┘            │Policy Engine │               │
│                           │ (三层证据)   │               │
│  sched/                   └──────┬───────┘               │
│  ┌──────────────┐               ↓                       │
│  │ ScxExecutor  │←──sched_ext──┼──cgroup_v2──→┌───────┐│
│  │ (OLK-6.6)    │              │              │Cgroup ││
│  └──────────────┘              │              │Executor││
│                                ↓              └───────┘│
│                         ┌──────────────┐               │
│                         │Skill Manager │               │
│                         │ (4 skills)   │               │
│                         └──────────────┘               │
│                                                          │
│  bench/ → Redis + Nginx 实验 → results/ + reports/      │
└─────────────────────────────────────────────────────────┘
```

## 快速开始（最小 3 步）

```bash
# 1. 检查环境
./scripts/check_env.sh

# 2. 编译
make agent

# 3. 运行（默认 dry-run 模式，观察系统进程）
./build/eulerpilot-agent --config configs/agent.yaml
```

预期输出：每条 `[Analyzer]` 日志包含进程名、PID、分类结果、评分、决策原因、执行动作。

## Skills 一览

EulerPilot 内置 4 个 Runtime Skill，通过 `skills.yaml` 控制启停：

```bash
# 查看已注册的所有 Skill
./build/eulerpilot-agent --list-skills
# 输出：
# network_policy_demo
# psi_gate
# resource_control
# security_policy_demo

# 探测每个 Skill 的可用性
./build/eulerpilot-agent --doctor-skills --config configs/agent.yaml
```

### resource_control（默认启用）
**做什么**：封装 `CgroupExecutor`（SP3 主线）和 `ScxExecutor`（OLK-6.6 增强线），将 workload 分类结果转化为实际的资源控制动作。

**已解决问题**：
- 识别 Redis/Nginx 为 `LATENCY_SENSITIVE`，stress-ng 为 `BACKGROUND_NOISY`
- 自动分配进程到 `/sys/fs/cgroup/eulerpilot/{latency,batch,background}`
- 动态调整 `cpu.weight`（latency=1000, background=20）

**怎么用**：
```bash
# 默认后端 cgroup_v2（SP3 可用）
./build/eulerpilot-agent --config configs/agent.yaml

# sched_ext 后端（仅 OLK-6.6 / 122 可用）
./build/eulerpilot-agent --config configs/agent.yaml --backend sched_ext
```

### psi_gate（默认启用）
**做什么**：封装 `PsiGate v1` 状态机，读取 `/proc/pressure/cpu`，配合 eBPF 调度指标做分层门控决策。

**状态机**：`NORMAL → ARMED → ACTIVE → COOLDOWN`

**已解决问题**：避免在负载轻微波动时频繁切换控制策略，只在压力窗口真正打开时才进入强控制。

### network_policy_demo（默认关闭）
**做什么**：基于 `cgroup/connect4` 的 eBPF 网络策略演示，对目标端口执行 deny。

**最小 demo**：
```bash
# 1. 构建 demo BPF 程序
make network-policy-demo

# 2. 启动 HTTP 服务
python3 -m http.server 18080 --bind 127.0.0.1 &

# 3. 编辑 configs/skills.yaml，将 network_policy_demo.enabled 改为 true

# 4. 启动 Agent（自动 attach BPF）
./build/eulerpilot-agent --config configs/agent.yaml

# 5. 测试 deny（需将进程移入 demo-net cgroup）
echo $$ > /sys/fs/cgroup/eulerpilot/demo-net/cgroup.procs
curl http://127.0.0.1:18080/   # → 000（连接被拒绝）

# 6. 停止 Agent（自动 detach + 清理）
# curl 恢复正常
```
预期：cgroup 内 `curl` 被拦截 → `000`，cgroup 外 `curl` 正常 → `200`。

### security_policy_demo（默认关闭）
**做什么**：基于 `BPF LSM file_open` 的安全策略演示，拦截对指定路径的文件访问。

**最小 demo**：
```bash
# 1. 构建 demo BPF 程序
make security-policy-demo

# 2. 查看测试文件
cat demo/security_policy_demo/secret.txt  # → "TOP SECRET"

# 3. 编辑 configs/skills.yaml，将 security_policy_demo.enabled 改为 true

# 4. 启动 Agent（自动 attach LSM）
./build/eulerpilot-agent --config configs/agent.yaml

# 5. 测试 deny
cat demo/security_policy_demo/secret.txt  # → Operation not permitted

# 6. 停止 Agent（自动 detach）
cat demo/security_policy_demo/secret.txt  # → "TOP SECRET"
```
预期：Agent 运行时 cat 被拒绝，退出后恢复访问。

## 配置文件说明

| 文件 | 用途 |
|------|------|
| `configs/agent.yaml` | 主配置（间隔、时长、后端选择、门控模式、Skills 路径） |
| `configs/policy.yaml` | 策略配置（profile 定义、分类阈值） |
| `configs/psi_gate.yaml` | PsiGate 配置（阈值、冷却时间） |
| `configs/skills.yaml` | Skills 清单（启停、种类、私有参数） |

## 赛题覆盖

| 方向 | 实现 | 等级 |
|------|------|------|
| resource control agent | CgroupExecutor + ScxExecutor，进入 Redis/Nginx 主实验 | 主线 |
| network policy agent | `network_policy_demo`（cgroup/connect4） | 可演示 |
| security policy agent | `security_policy_demo`（BPF LSM file_open） | 可演示 |

## 环境

| 环境 | IP | 角色 |
|------|-----|------|
| 主交付机 | 192.168.1.121 | 代码开发 + 文档 + cgroup v2 主闭环 |
| OLK 验证机 | 192.168.1.122 | sched_ext 正式对照（内核 6.6.0-olk66-scx） |
| GitHub | `shibuchou/EulerPilot` | 代码仓库（私密） |

## 目录结构

```
EulerPilot/
├── agent/              # Agent 主体（C++）
│   ├── src/            # 实现（main/runtime/executors/psi_gate/builtin_skills）
│   ├── include/        # 头文件
│   ├── observer/       # PSI reader
│   └── skills/         # Skill 描述文档
├── bpf/                # eBPF 程序
│   ├── workload_observer.bpf.c     # 调度观测
│   ├── network_policy_demo.bpf.c   # 网络策略 demo
│   └── security_policy_demo.bpf.c  # 安全策略 demo
├── sched/              # sched_ext 调度器
│   └── scx_eulerpilot.bpf.c
├── configs/            # YAML 配置
├── bench/              # 实验脚本
│   ├── redis/          # Redis 实验
│   ├── nginx/          # Nginx 实验
│   └── psi/            # PsiGate smoke
├── scripts/            # 辅助脚本（环境检查/rollback/cleanup/渲染）
├── docs/               # 文档
├── reports/            # 图表 + 报告
│   └── final_figures/  # 7 张 SVG
├── results/final/      # 冻结候选结果（RUNS=5）
├── demo/               # 演示材料
└── tools/              # 调试工具
```

## 文档导航

| 需要什么 | 看这里 |
|----------|--------|
| 了解项目全貌 | `docs/defense_final.md` |
| 看最终报告 | `docs/final_report_submission.md` |
| 看结果数据 | `docs/final_results_summary.md` |
| 准备答辩 | `docs/defense_slides_outline.md` + `docs/final_talk_script.md` |
| 现场演示 | `docs/demo_runbook.md` |
| 提交清单 | `docs/submission_checklist.md` |
| 新人接手 | `docs/handover_manual.md` |

## 结论边界

**可以说的**：系统实现完成，双后端真实运行，Redis/Nginx 双业务线 RUNS=5 候选结果，Skills 框架可扩展，network/security eBPF demo 可现场演示。

**不能说的**：sched_ext 全面优于默认调度器，所有场景都稳定带来收益。

> EulerPilot 已完成为一个面向 openEuler 的、可运行、可实验、可解释、可复现、可扩展的系统资源管控 Agent 工程闭环。
