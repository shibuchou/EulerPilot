# EulerPilot v2

面向 **2026 年全国大学生计算机系统能力大赛—操作系统设计赛—中西部与华南区域赛—功能挑战赛道**，围绕「面向 openEuler 的自适应资源管控 Agent」重新设计的独立项目。

## 参赛信息

- 高校：西安邮电大学
- 队伍：三驱兄弟
- 队伍 ID：T2026116649912743
- 赛题：proj2（功能挑战赛道）

**主验收系统为 openEuler 24.03-LTS-SP3**（SP4 因 cgroup v1/无 PSI 只作可选兼容验证，不替代 SP3 证据）；Ubuntu 用于开发。旧 EulerPilot、SchedPilot 作为参考资产按模块迁移；旧前端验收界面不迁入，以 CLI、结构化状态和可复现实验完成闭环。

## 当前状态（2026-10-24）

方案一句话：**用户态 Agent 负责感知与决策，特权执行服务是唯一写者，v2 自建 sched_ext 数据面执行；所有决策被声明式契约约束，失败时向"无策略"方向回退。** 效果域两个（`sched` 调度实例 / `cgroup` 的 `cpu.weight`），策略 kind 必须与实例的域一致。

| 阶段 | 状态 |
|---|---|
| P0 环境 | 完成（SP3 实机、内核/BTF/config 核验、参考实现干净重建） |
| P1 静态闭环 | **判定达成**（[P1 退出门槛报告](docs/p1/P1_EXIT_REPORT.md)：fail-safe 矩阵 run8 17/17 PASS）。不覆盖：adaptive/心跳/PMU 为 UNSUPPORTED 而非通过；stall 类缺陷未穷尽；崩溃后 pin 靠同实例重启回收。原"越权改 class 不纠正"已于 2026-10-25 由轮换抽样再断言闭合（真机两场景） |
| P2 自适应 | 功能件已实现并单独留证（FeatureWindow/规则路由/SLO 门控/服务观测/切换抑制/自动 apply），自动通道默认关闭；**门控的收益未测出**（只证明是"延迟器不是否决器"，推迟约 40s、代价上界 ≈0–30 个请求），未收口 |
| P3 性能 | 首批正式矩阵已出数（20 block × A0/A1/A2/C/D，seed 20261024，块内配对 + 10000 次 bootstrap）：C vs A0 goodput@5ms **+1.62% [+1.53,+1.82]（20/0）**、p99 −47.9%；C vs A2 **+1.90%**（不是静态权重倾斜）；A1 纯观测臂 **−0.82%**（观测栈是净成本）；BG 相对进度 0.997–1.001。限定：单机单负载单 SLO；@2ms 阈值处无收益；class 路由实测零结果；**新构建后须重跑**（见下一步） |
| P4 扩展 | **R5 两半均已真机闭环**：动作端 cgroup 效果域（cpu.weight,崩溃 marker 恢复）+ 感知端 eBPF 事件链（raw_tp 采集 runnable 等待 → 只读泵 ep-evtmon → event.observe → 武装守卫经事务路径升权/补偿;杀泵门冻结+TTL 兜底）,`integration_events` 为第 8 个 CTest 必测项。见 `evidence/p4/events-link-20261025.txt` |
| P5 交付 | 未开始 |

回归基线（2026-10-25 冻结构建 `370180b`）：unit **550** 条断言、`ctest` **8/8**、验收 **r28** 六轮 rc=0、fail-safe **run9 17/17**；旗舰矩阵按同一冻结参数重跑中,重跑前 P3 数字已在同一冻结集上重跑复现（C vs A0 GET **+1.83% [1.38,2.26] 20/0**、p99 −39.6%；D vs C 新构建统计打平；见 `evidence/p2/flagship-rerun-20261025.txt`）。补充对照：B（BASIC 基础成本）+1.75% 10/0、E（参考 daemon）GET +1.92% 10/0（`barms-20261025.txt`、`earms-20261025.txt`）。

阶段判定依据、逐项证据路径、硬天花板（内核行为与测量能力的边界）与工程约定见 [交接件](docs/HANDOVER_20261024.md)——接手本项目的第一份读物；进度明细见 [实施计划](docs/IMPLEMENTATION_PLAN.md)。

## 从这里阅读

| 文档 | 用途 |
|---|---|
| [完整方案](docs/v2_redesign_2026.md) | 技术路线、架构、算法、故障恢复、创新边界 |
| [交接件（2026-10-24）](docs/HANDOVER_20261024.md) | 方案摘要、当前进度与不覆盖项、硬天花板、下一步优先级、仓库/远端状态、证据索引 |
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
| [P1 退出门槛判定](docs/p1/P1_EXIT_REPORT.md) | 逐项门槛的达成/未达成、证据层级与不证明什么 |
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

强制 8 个 CTest 必测项无 SKIP（含 `integration_cgroup` 与 `integration_events`：cgroup 域真机闭环与 R5 事件链五相时间线）、A/B/C liveness、产物哈希和收尾状态断言；**不包含性能结论**。多轮驱动用 `scripts/accept_sp3_rounds.sh`。

SP3 协议 §8 fail-safe 矩阵（故障恢复层，17 条目，需要 root、sched_ext idle 且 pin 根为空）：

```bash
bash experiments/fail_safe_matrix_sp3.sh /root/epv2-build/ep-executord /root/epv2-build/ep \
  /root/epv2-build/ep-sched-loader /root/epv2-evidence/fail-safe-$(date +%Y%m%d)
```

它注入的是控制面与内核故障（崩溃、租约/TTL、非法与越界策略、generation 冲突、双实例、外部改值、成员迁出、日志盘满、CPU 热插拔、两步写入的第二步失败），逐条判定"回 default fair 后目标是否继续推进、残余效应是否被内核 map 自证清除"，**不测性能**。2026-10-24 run8 结果 **17/17 PASS、rc=0**（同日 run4 曾以 S14 FAIL 收尾，该缺陷已修复并重验，失败现场保留），见 [fail-safe-20261024.txt](evidence/p1/fail-safe-20261024.txt) §7 与 [P1 退出门槛判定](docs/p1/P1_EXIT_REPORT.md)。

## 设计重点

- SchedPilot 的多队列、唤醒抢占、分类器和实验方法作为起点，先移植静态 C 路径，再验证路由和自适应是否带来增益。
- EulerPilot 的目标解析、Skill、审计与恢复思路保留；控制面重写为轻量 Agent 与唯一特权写入服务。
- 按业务角色与运行特征分别建模；BACKGROUND 来自显式授权，不能凭 CPU 占用猜测。
- SP3 上的真实 sched_ext 闭环是硬门槛；缺能力时只允许标记为降级运行，不算完成赛题核心。
- 以服务 SLO、后台最小进度、静默开销和故障恢复共同约束性能优化。

## 下一步

按优先级（每项的完成判据见 [交接件](docs/HANDOVER_20261024.md) §5）：

1. ~~R5 后半：eBPF 事件触发链路~~ **已完成（2026-10-25,真机五相时间线）**；剩余：watch 的 cgroup 重建身份拒绝、泵开销量化。
2. **在冻结构建上重跑 A1/C/D 矩阵臂**：P1 收尾的四项控制面改动都晚于旗舰构建，重跑后 P3 数字才与交付构建一致；新旧并列报告，不许只留好看的那组。
3. ~~class 逐条漂移的处置~~ 已完成（2026-10-25,抽样再断言,真机两场景）。
4. ~~R6 对照臂~~ B 臂与 E 臂已完成（2026-10-25）：B vs A0 **+1.75%**（收益在 BASIC 形态就出现,class 额外贡献≈0）;E vs A0 GET **+1.92% [0.94,2.17]**、SET 打平（新旧控制面同量级）。X 臂：SP3 上无上游 scx 二进制,待构建后再评估。
5. **P5 交付**：构建再冻结 → 受影响矩阵重跑 → SP3 镜像/手册 → 演示三次彩排 → 包校验 → 含负结果的验收报告。

## 许可

本项目采用 **GPL-2.0**（2026-10-21 决定），全文见 [LICENSE](LICENSE)。组件级边界见 [NOTICE](NOTICE)：
`sched/` 为 SchedPilot 派生的 GPL-2.0 组件，`third_party/nlohmann` 为 MIT，旧 EulerPilot 仅作参考。
