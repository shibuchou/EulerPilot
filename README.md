# EulerPilot v2

面向 **2026 年全国大学生计算机系统能力大赛—操作系统设计赛—中西部与华南区域赛—功能挑战赛道**，围绕「面向 openEuler 的自适应资源管控 Agent」重新设计的独立项目。

## 参赛信息

- 高校：西安邮电大学
- 队伍：三驱兄弟
- 队伍 ID：T2026116649912743
- 赛题：proj2（功能挑战赛道）

**当前状态：P0 技术验证通过，P1 部分实现且未收口，P2 为 advisory 切片。控制面、v2 自建 loader（每实例 pin、`eulerpilot` ops）、cfg 参数写入、SchedulingPolicy 校验与 cgroup/pid 目标绑定（cgroup_id、单次apply持有 pidfd/starttime）、租约/策略 TTL 过期自动回退、apply deadline 强制已跑通 SP3 真实闭环；执行服务通过 FD 传递持有 link FD；FeatureWindow、advisory 规则路由、SLO 门控与非人工来源 SLO 否决已接入；A/B/C liveness 已通过；显式 BACKGROUND 的 pid/cgroup 目标已展开到 class_map 并由执行服务每 2s 刷新；helper 已加整轮刷新预算（204ms/3 pid）和同期限 EOF 收割（208ms）。自动 apply、加载场景 SLO 与性能比较尚未实现，尚无 v2 性能结果。** 主验收系统为 openEuler 24.03-LTS-SP3；Ubuntu 用于开发和辅助兼容验证。旧 EulerPilot、SchedPilot 作为参考资产，按模块迁移。旧前端验收界面不迁入，先用 CLI、结构化状态和可复现实验完成闭环。

2026-10-21 已完成 P0（SP3 环境、参考数据面干净重建、A/B/C smoke）与 P1 主体：`ep-executord` 唯一写者、lease/fence、generation、append-only WAL、事务状态机、崩溃 reconcile、幂等，`ep` CLI，`sched`（loader 一次性 attach 后通过 SCM_RIGHTS 把 link FD 交给执行服务并退出；每实例 pin、`eulerpilot` ops）与 `sched-ref`（子进程对照模式），短命 `ep-schedctl` 写入/回读/恢复 cfg 参数，以及策略 schema/语义校验与 cgroup/pid TargetBinding。既有SP3日志记录 `ctest` 6/6通过；审查发现的故障边界尚未覆盖，不能据此判定P1完成。

## 从这里阅读

| 文档 | 用途 |
|---|---|
| [完整方案](docs/v2_redesign_2026.md) | 技术路线、架构、算法、故障恢复、创新边界 |
| [12轮后核验与决策](docs/REVIEW_20261021.md) | 当前缺口、自动apply准入和本轮验证 |
| [实施计划](docs/IMPLEMENTATION_PLAN.md) | 阶段、任务、退出条件和当前进度 |
| [实验协议](docs/EXPERIMENT_PROTOCOL.md) | 公平对照、负载、指标、统计、消融和证据格式 |
| [接口契约](design/INTERFACES.md) | Tool/Skill、调度 ABI、事务和错误处理 |
| [相对原稿的修改](docs/DELTA_FROM_V0_1.md) | 为什么修正 SP3/SP4、默认模式、策略路由等设计 |
| [赛题基准](docs/CONTEST_REQUIREMENTS.md) | 本次用户给出的赛题要求及验收映射 |
| [P0 环境报告](docs/p0/SP3_ENV_REPORT.md) | SP3 实机复验结果、启动参数改动与剩余项 |
| [P1 骨架报告](docs/p1/P1_SKELETON_REPORT.md) | 执行服务/CLI 实现、测试结果、契约偏差与未实现项 |
| [P1 SchedSkill 报告](docs/p1/SCHED_SKILL_REPORT.md) | v2 自建 loader 的真实 attach/detach、崩溃回收与边界 |
| [策略与绑定报告](docs/p1/POLICY_BINDING_REPORT.md) | SchedulingPolicy 校验、cgroup TargetBinding 与实测 |
| [P1 ABI 冻结](docs/p1/ABI_FREEZE.md) | 已实现的 v1alpha1 子集、线路格式、幂等与 reconcile 规则 |
| [来源与资产](PROVENANCE.md) | 旧仓库版本、输入快照、许可和迁移边界 |
| [EulerPilot 审计](docs/audits/eulerpilot_audit.md) / [SchedPilot 审计](docs/audits/schedpilot_audit.md) | 从旧实现和实验中提炼的依据 |
| [外部资料核验](docs/research/sources.md) | 内核、scx、参考论文和证据限制 |

## 现在可执行的检查

设计契约与环境只读探测（不安装依赖、不加载 BPF、不改变调度策略）：

```bash
cd /mnt/d/code/Ubuntu/EulerPilot-v2
python3 scripts/validate_design.py
python3 scripts/doctor.py
```

`validate_design.py` 需要已有的 `jsonschema`；`doctor.py` 仅使用 Python 标准库。`doctor.py --require-scx` 在缺少 sched_ext 能力时返回非零；成功的只读探测仍不等于真实加载测试通过。

SP3 根目录的一键验收（需要已构建、sched_ext idle）：

```bash
bash scripts/accept_sp3.sh /root/epv2-build /root/epv2-src
```

新版强制 6 个 CTest 必测项无 SKIP、A/B/C liveness、产物哈希和收尾状态断言；
**不包含性能结论**。2026-10-22 已在 SP3 重跑通过，输出见
[acceptance-sp3-strict-20261022.txt](evidence/acceptance-sp3-strict-20261022.txt)；
旧脚本历史输出保留为 [acceptance-sp3-20261021.txt](evidence/acceptance-sp3-20261021.txt)。

## 设计重点

- SchedPilot 的多队列、唤醒抢占、分类器和实验方法作为起点，先移植静态 C 路径，再验证路由和自适应是否带来增益。
- EulerPilot 的目标解析、Skill、审计与恢复思路保留；控制面重写为轻量 Agent 与唯一特权写入服务。
- 按业务角色与运行特征分别建模；BACKGROUND 来自显式授权，不能凭 CPU 占用猜测。
- SP3 上的真实 sched_ext 闭环是硬门槛；缺能力时只允许标记为降级运行，不算完成赛题核心。
- 以服务 SLO、后台最小进度、静默开销和故障恢复共同约束性能优化。

下一步：先修复FD/helper、WAL回退、active目标身份和数据面公平性，再做显式policy+lease自动apply与性能pilot。SP3 已可用；SP4 也已启动（`<SP4-IP>`），但只作可选兼容验证，不替代 SP3。详细状态见 [实施计划](docs/IMPLEMENTATION_PLAN.md)。

## 许可

本项目采用 **GPL-2.0**（2026-10-21 决定），全文见 [LICENSE](LICENSE)。组件级边界见 [NOTICE](NOTICE)：
`sched/` 为 SchedPilot 派生的 GPL-2.0 组件，`third_party/nlohmann` 为 MIT，旧 EulerPilot 仅作参考。
