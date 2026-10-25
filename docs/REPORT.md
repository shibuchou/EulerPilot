# EulerPilot v2 参赛文档

**选题 ID:proj2《面向 openEuler 的自适应资源管控 Agent》**(功能挑战赛道)
**队伍**:西安邮电大学 · 三驱兄弟 · T2026116649912743
**验收系统**:openEuler 24.03 LTS-SP3,内核 `6.6.0-schedpilot-sp3`(自编译,sched_ext 可用)

| 文档名称 | EulerPilot v2 参赛文档 |
|---|---|
| 文档版本 | v1.1 |
| 文档类型 | 参赛文档(按大赛文档大纲组织) |
| 文档状态 | 正式发布 |
| 适用对象 | 评审专家、验收人员 |
| 维护团队 | 三驱兄弟(西安邮电大学) |
| 更新日期 | 2026-10-25 |
| 配套文档 | 项目说明书 / 设计文档 / 用户手册 / 部署文档 / 测试文档 / `ACCEPTANCE_20261025.md` |

> 纪律声明:本文每个数字都有仓库内证据文件支撑(随行标注文件名);未实测的不写
> 百分比;做不到的明确写 UNSUPPORTED 并给理由。文档结构按大赛文档大纲组织。

## 1. 目标描述

构建一个**可扩展的用户态资源管控 Agent**:感知 workload 特征,按负载动态选择资源
策略,重点经由 sched_ext 改善 CPU 调度,并可扩展 eBPF hook 连接 cgroup 等资源域。
交付必须能在 openEuler 24.03-LTS-SP3 上正常编译、运行和测试。

我们把这一目标落成四条可验收的硬指标:

1. **真实闭环**:sched_ext 调度器在 SP3 上真实挂载/验证/回退,内核状态自证;
2. **工程可靠**:唯一特权写者、声明式契约、崩溃可恢复(fail-safe 矩阵 17 条目);
3. **性能可证**:预注册实验协议下的配对统计结论,含负结果与敏感性;
4. **扩展完整**:R5 的 hook→决策→动作→恢复 完整链路真机跑通。

## 2. 比赛题目分析和相关资料调研

**需求拆解**(原文映射见 `docs/CONTEST_REQUIREMENTS.md` R1–R10):赛题的关键词是
"选择**或**生成"——允许先做可靠的策略**选择**,DSL 生成是增强;"标准化 Tool/Skills"
解释为版本化 manifest、输入输出 schema、统一生命周期与可测试契约(未强制 MCP/LLM);
评分权重创新 30、功能 25、性能 25、代码质量 10、演示 10。

**资料调研**(`docs/research/sources.md` 有一手链接核验):

- sched_ext/scx:6.6 内核的结构与发布顺序(state→uevent→`scx_enable_seq` 最后递增)、
  `bpf_struct_ops_link` 无在位查询、第二次 struct_ops 注册 EBUSY——这些都直接塑造了
  我们的在位性判定设计;
- 论文方向(Towards Agentic OS、MoS/ASA):专家路由、SLO 门控已有工作;我们把
  **改进假设**放在约束链、发布一致性与实测闭环上,复用思想如实归属(`PROVENANCE.md`);
- 旧资产审计(`docs/audits/`):EulerPilot 旧类队列路径在旧环境有退化记录,SchedPilot
  的数据面与实验方法是起点——v2 从零重建控制面,数据面按 GPL-2.0 派生迁移并逐文件
  记录哈希。

**由此定下的三个设计前判**:SP3 是唯一验收级系统(SP4 实测 cgroup v1/无 PSI,只能
UNSUPPORTED);BG 是业务角色、LAT/COMPUTE/CACHE 是运行特征,两维度正交;自适应
动作必须被租约、门控与抑制三层约束。

## 3. 系统框架设计

```
┌─ 决策层(默认 advisory)────────────────────────────────┐
│ FeatureWindow(CPU/load/PSI)→ 规则路由 → SLO 门控 → AutoPilot │
│ 服务观测(service.observe)/ 事件观测(event.observe)——只能更严 │
└───────────────┬──────────────────────────────────┘
                │ 声明式策略(JSON Schema + 语义校验)
┌───────────────▼──────────────────────────────────┐
│ ep-executord 唯一特权写者:flock 单实例、lease/fence、    │
│ generation、append-only WAL、事务状态机、幂等、崩溃 reconcile│
└───────┬──────────────────────────────────┬───────┘
   sched 域                            cgroup 域
 v2 自建 loader(ops `eulerpilot`),       cpu.weight(写后读回 +
 link FD 经 SCM_RIGHTS 交执行服务持有      marker 崩溃恢复 + 事件守卫)
└───────┴──────────────────────────────────┴───────┘
      内核:sched_ext maps / cgroup v2 / raw tracepoints
```

- **两个效果域共用一套事务骨架**;策略 kind 与实例域不匹配零副作用拒绝。
- **契约**:`design/schemas/*.json` 五个版本化 schema + `EventArm`,设计自检 6 正例/
  27 反例强制通过;所有写操作走同一条 lease/fence/generation/WAL 通道。
- **R5 事件链**:raw tracepoint BPF 测每 cgroup 的 runnable 等待 → 只读泵 `ep-evtmon`
  (0.5% 单核,12.1k 事件/秒零丢弃)→ `event.observe` → 人工授权的武装守卫经**同一条
  事务路径**调 `cpu.weight` → 条件清除/TTL/泵死亡三条路自动补偿;同路径 cgroup 重建
  会使 arm 失效(WATCH_REBUILT),权限不静默转移。
- **观测纪律**(`docs/EXPERIMENT_PROTOCOL.md` §5.1):比较类判定先声明分辨率,策略
  敏感度不大于实测噪声时明报 `not run`;观测断供必须可报告;生产端周期必须严格快于
  新鲜度判据。

## 4. 开发计划

按阶段退出条件排期(`docs/IMPLEMENTATION_PLAN.md` 全表):

| 阶段 | 内容 | 退出判定 | 实际 |
|---|---|---|---|
| P0 环境 | SP3 实机、内核核验、参考重建 | attach/detach + smoke | 10-21 完成 |
| P1 静态闭环 | 控制面、loader、策略绑定 | fail-safe 全过、无失速 | 10-24 达成(fail-safe 17/17) |
| P2 自适应 | 观测/路由/门控/自动 apply | 相位可观察、缺指标保守退化 | 10-22~23 完成;门控收益量化为"延迟器" |
| P3 复现/性能 | 冻结参数、正式矩阵 | 原始件+配对统计 | 10-23 首轮;**10-25 冻结构建复现** |
| P4 扩展/恢复 | cgroup 域 + eBPF 事件链 | 一条真实 hook→…→恢复 | 10-24 动作域;**10-25 感知端闭合** |
| P5 交付 | 冻结、镜像、手册、彩排、验收报告 | 包校验全过 | 10-25 完成(镜像 14.6GiB) |

## 5. 比赛过程中的重要进展

- **10-21(P0)**:SP3 环境与参考数据面干净重建,A/B/C smoke;同日完成控制面骨架
  (唯一写者/WAL/事务)与真实 attach。
- **10-22**:数据面公平性修正(份额由片长比决定、真实墙钟记账,60% 预算实测
  LAT 57.9–58.5%);`enable_seq` 发布窗口根因修正(内核发布顺序,曾致 7 次验收败 2);
  自动 apply 最小切片 + 切换代价量化(303–665ms 空窗)⇒ 三层抑制。
- **10-23**:`runnable task stall` 根因定位(亲和性改投)与修复(`enq_pinned_local`,
  修复前 6/6 失败 → 3×90s 零失速);class 路由接入控制面;旗舰参数冻结;**首轮正式
  矩阵**(20 block × 5 臂)出数:C vs A0 +1.62%。
- **10-24**:P1 收口——fail-safe 矩阵 17/17(含盘满、两步写失败);cgroup 域上线;
  交接件与退出门槛判定。
- **10-25**:R5 感知端补齐(事件链六相真机时间线);class 越权漂移抽样纠正;冻结
  构建上**旗舰复现 +1.83% [1.38,2.26](20/0)**;B/E 补充臂(收益归因链闭合);
  D-no-class 对照(联合 SLO 2/3 = D);演示三连彩排;交付镜像与包校验。

## 6. 系统测试情况

| 层 | 内容 | 结果 |
|---|---|---|
| 设计 | schema 正例/反例 | 6 正例 / 27 反例强制 |
| 单元 | 587 断言(判定真值表、门控矩阵、崩溃恢复、双写拒绝) | 全过 |
| 集成 | 8 个 CTest 必测项(真内核挂载、cgroup 域、事件链六相) | 8/8 |
| 验收 | `accept_sp3.sh` 六轮制(强制无 SKIP+哈希+收尾断言) | r28/r29/r30 均 6/6 |
| 故障恢复 | fail-safe 17 条目 ×3 代构建 | run9/10/11 均 17/17 |
| 漂移 | cfg 逐周期再断言;class 轮换抽样(越权改/删被纠正) | 真机两场景 |
| 性能 | 预注册矩阵 + 10000 次配对 bootstrap | 见下节 |
| 包 | 哈希/证据/契约三查 | 全过 |

**性能主表**(C=固定专家,A0=默认 fair,A1=纯观测栈,A2=静态 cgroup 倾斜,B=BASIC
数据面,D=自适应,E=参考 schedpilotd;块内配对,原始逐请求件留 SP3):

| 对照 | goodput@5ms | 胜/负 | p99 |
|---|---|---|---|
| **C vs A0** | **GET +1.83% [1.38,2.26] / SET +1.39%** | **20/0、18/2** | −39.6%/−32.8% |
| C vs A2 | +1.90%(20/0) | — | 收益非静态倾斜可解释 |
| D vs A0 | +1.74%/+1.05% | 19/1 | −37.9% |
| A1 vs A0 | −0.55~−0.82%(观测栈成本,如实披露) | — | +5~14% |
| B vs A0 | +1.75% [1.45,2.09] | 10/0 | −40.8% |
| E vs A0 | GET +1.92% [0.94,2.17];SET 打平 | 10/0 | −49.4% |
| BG 相对进度 | 0.998–1.006(未饿死后台) | — | — |

敏感性同报:@2ms 无优势;D vs C 复跑统计打平;单机单负载单 SLO;跨机 UNSUPPORTED。
归因链:A1(−0.5%)→B(+1.75%)→C(+1.83%),class-routing 消融为零、Dnc=D——
**class 声明是可审计的契约,不是性能开关**。

## 7. 遇到的主要问题和解决方法

| 问题 | 根因 | 解法 | 证据 |
|---|---|---|---|
| 验收间歇性 `enable_seq changed` | 内核 `scx_enable()` 最后才递增计数器,apply 读到旧值 | 以 pre-enable 值为基准等发布稳定,等值判定 | `enable-seq-publish-20261022.txt` |
| 挂载后 ~46s 被内核摘除(runnable task stall) | 绑核任务被改投全局 DSQ,无人唤醒其 CPU | `nr_cpus_allowed==1` 直投本机 local DSQ + `SCX_KICK_IDLE` | `stall-runnable-20261023.txt` |
| 盘满时 `ep rollback` 被租约前置写挡死 | `lease.acquire` 要写 state.json | 方向不对称:存储失败授"仅内存租约",拆效应放行、建新效应拒绝 | fail-safe S14 |
| 静默秒退的 loader | 干净构建丢了 `-DEP_LIBBPF_STATIC`,系统 libbpf 行为不同 | CMake 强警告 + 手册记录配方 | `frozen-build-20261025.txt` |
| 新建 cgroup 写 cpu.weight 报 EACCES | 本机根/system.slice 默认只委派 memory pids | 测试用 `systemd-run -p CPUWeight` 建 scope | 手册陷阱 2 |
| 事件测试偶发失败 | 负载稳态(3.8–4.3ms)贴着 5ms 阈值 | 海绵降权 nice -10,判定阈值两侧留余量 | `demo-rehearsals-20261025.txt` |
| p99 无法驱动自动判定 | 同状态窗间噪声 46% >> 声明预算 5% | 分辨率护栏:敏感度不足明报 not run | 协议 §5.1 |

## 8. 分工和协作

【以下按模块起草,请按队伍实际情况修订人名与比例】

- 内核与数据面:sched_ext 内核构建、BPF 数据面(切片/预算/stall 修复)、事件 BPF;
- 控制面与执行服务:唯一写者/事务/WAL/恢复、CLI、两域 Skill 与契约 schema;
- 实验与证据:实验协议、预注册矩阵、fail-safe 矩阵、性能分析与统计;
- 文档与交付:手册、验收报告、演示脚本与彩排、镜像打包与溯源。

协作方式:声明式契约先行,schema 变更必须同步示例/校验/文档;实验改动晚于冻结
构建的必须重跑对应矩阵;证据文件含完整性与边界章节,负结果与失败现场全部保留。

## 9. 提交仓库目录和文件描述

```
README.md                 评委入口 + 题目 ID/内容 + 状态总览
docs/REPORT.md            本文(大赛大纲结构)
docs/ACCEPTANCE_20261025.md  R1–R10 逐条验收判定与边界
docs/MANUAL_SP3.md        构建/运行/演示手册(含三个实测陷阱)
docs/VIDEO_SCRIPT.md      演示视频逐字讲稿
docs/IMPLEMENTATION_PLAN.md 实施计划全表(逐项带证据路径)
docs/v2_redesign_2026.md  完整设计
design/                   契约 schema 与示例(5 个 + EventArm)
src/ core executor cli skills evtmon   控制面/执行服务/CLI/Skill/事件泵
sched/                    数据面:BPF、loader、cfg helper、事件 tracing
experiments/              全部实验驱动(旗舰/消融/fail-safe/演示/开销)
tests/                    单元 + 8 项集成(含事件链六相)
evidence/                 p0/p1/p2/p4/p5 证据文件(每个含完整性与边界)
PROVENANCE.md             逐轮二进制哈希、来源与公开化重写披露
scripts/                  验收/校验/演示/包校验/构建脚本
LICENSE NOTICE NOTICE     GPL-2.0 与组件边界
```

交付镜像:`schedpilot-sp3-20261025.qcow2`(虚拟 80GiB/压缩 14.6GiB,SHA256 与
domain.xml/ks.cfg 同包;含本仓库源树与冻结二进制;`PROVENANCE.md` 末节)。

## 10. 比赛收获

1. **测量先于结论**:p99 窗间噪声 46% 教会我们"比较类判定先声明分辨率";两个
   阈值敏感性反转(2ms 处无收益)如果不主动报,结论就是自欺。
2. **唯一写者是架构不是口号**:把 LLM、脚本、自动通道全部收进同一条
   lease/generation/WAL 管道后,"谁是第二写者"从争论变成结构上不可构造。
3. **失败方向可以不对称**:拆效应永远放行、建新效应必须有可持久化审计——盘满
   场景的优雅解法来自这条原则,而不是来自某个具体技巧。
4. **消融比胜率更重要**:B 臂、class-routing 零、Dnc=D 三条线让我们敢说"收益
   来自数据面切片形态、class 是契约不是开关"——这句话比 +1.83% 更有信息量。
5. **UNSUPPORTED 也是交付物**:跨机、X 臂、adaptive 各自有实证理由;把"没做"
   说清楚,与把"做了"说清楚同样构成可信度。

## 11. 修订记录

| 版本 | 日期 | 内容 | 作者 |
|---|---|---|---|
| v1.0 | 2026-10-25 | 按大赛文档大纲创建 | 三驱兄弟 |
| v1.1 | 2026-10-25 | 增加文档信息表与修订记录;与五份交付文档统一为蓝底表头 Word/PDF 交付版 | 三驱兄弟 |
