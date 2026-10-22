# 外部资料核验与设计边界

核验日期：2026-10-21。通过 Firecrawl `search` 定位、`scrape` 阅读一手页面；未使用搜索摘要替代关键技术依据。工具可能返回近期索引内容，本次访问不等同于对上游实时 HEAD 或实机行为的证明。本文仅核验设计依据，没有执行新项目的内核加载、性能实验或正式交付验收。

## 1. 直接影响方案的结论

| 命题 | 本次结论 | 对 v2 的约束 |
|---|---|---|
| “Linux 6.6 一定没有 sched_ext” | 不成立：上游 6.12 合入不排除发行版回移；openEuler 有 OLK-6.6 回移邮件 | 按实际源码、内核配置、BTF/kfunc 和加载结果探测 |
| “只有 SP4 能完整运行” | 未找到足以支持该绝对判断的一手证据；本地另有 SP3 自定义内核历史记录 | SP3 为主交付门槛，SP4 为附加验证环境 |
| “赛题 SP3 文案过期” | 未核验，不能作为项目假设 | 以用户提供赛题要求为准；若涉及自定义内核合法性，单独向组委会确认 |
| “MoS 的方法是在线 bandit” | 论文描述的主方法是离线分类、时间投票、机器相关映射和在线调度器切换 | bandit 是本项目可选研究扩展，不能冒充论文复现 |
| “专家策略路由本身是原创” | 已有 MoS/ASA；Agent 与执行平面解耦也已有 SchedCP | 创新点必须落到本项目具体机制及消融实验 |
| “partial switch 是无影响的局部调度” | 只让 SCHED_EXT 任务进入 scx；普通 fair 类与 scx 的竞争关系仍须核验 | 不以 partial switch 自动获得公平隔离，实验需记录 switch scope |

## 2. Linux sched_ext 语义

**来源 S1：** [Linux 6.12: Extensible Scheduler Class](https://docs.kernel.org/6.12/scheduler/sched-ext.html)；访问日期 2026-10-21；已阅读全文相关章节。固定使用 6.12 版本文档，避免滚动版文档混入尚未适配的接口。

已确认：需要 `CONFIG_SCHED_CLASS_EXT` 及 BPF/BTF 等配套配置；加载默认接管 normal/batch/idle/ext，设置 `SCX_OPS_SWITCH_PARTIAL` 则只接管 SCHED_EXT。调度程序终止、SysRq-S、错误或 runnable stall 会触发退出，回到默认 fair 类。BPF 调度器 ABI，包括 ops 和 kfunc，没有稳定保证。

设计含义：锁定内核源码、配置、scx 头文件和 BPF 构建产物；CO-RE 不能替代 sched_ext ABI 适配。必须分别测试 Agent 失联、loader 退出、link 生命周期和 watchdog。该文档不能证明某个 openEuler 回移版本与上游具有完全一致的实现，也不保证业务 SLO。

## 3. 上游 scx

**来源 S2：** [sched-ext/scx](https://github.com/sched-ext/scx)；访问日期 2026-10-21；已阅读 README。

已确认：上游 sched_ext 从 Linux 6.12 起合入；scx 提供多种调度器及工具，运行前提是内核具备相应支持。当前页面提示 C 示例已迁往 [scx-c-examples](https://github.com/sched-ext/scx-c-examples)，主仓库用户态部分以 Rust 为主；还提供 breaking changes 文档。

设计含义：新项目集成 scx 应明确兼容 commit 和基线调度器，不能把“拉取最新版即可构建”当承诺。SchedPilot 的已有 C/C++ 数据面可以作为首批迁移对象，上游独立调度器另作性能对照。README 不能证明其当前主分支可以直接运行在团队的 openEuler 6.6 回移内核；本次未做编译验证。

## 4. openEuler SP3 与 OLK：证据分级

**来源 S3：** [openEuler 内核邮件：PATCH OLK-6.6 161/451 sched_ext: Add boilerplate for extensible scheduler class](https://mailweb.openeuler.org/archives/list/kernel@openeuler.org/message/YKOYR2F6VPIYFBIRRYDOOJXE5XMLOTXZ/)；访问日期 2026-10-21。邮件日期为 2026-04-03，注明从 mainline-v6.12-rc1 回移，并列出源提交。

已确认：OLK-6.6 存在 sched_ext 回移工作，不能用“6.6 < 6.12”判定完全无实现。未确认：该系列全部合入状态、进入哪些 SP3 RPM、默认配置、团队安装包与源码是否一致；单封 boilerplate 补丁也不能证明功能完整。

尝试读取 `https://gitee.com/openeuler/kernel/raw/openEuler-24.03-LTS-SP3/kernel/sched/Kconfig` 返回 Repository or file not found。该读取失败不构成“SP3 没有实现”的证据，可能涉及分支或仓库路径变化；未继续猜测发行内核能力。

**本地历史证据 L1（非外部文献）：** `D:/code/Ubuntu/schedpilot/evidence/sp3-vm/README.md`、`env_check.json`、`build.log`、`sp3-hello/experiment.meta.json`。本次只读核查：记录日期为 2026-10-05，OS 为 SP3，运行内核 `6.6.0-schedpilot-sp3`，配置含 `SCHED_CLASS_EXT=y`；构建日志引用 `/usr/src/linux-6.6.0-145.3.34.165.oe2403sp3.x86_64/tools/sched_ext`；Redis A/D 仅 2×20 秒、PMU 不可用，meta 的 git_commit 为 unknown。

这些记录支持“团队已有在 SP3 用户态环境搭配自定义内核运行的历史路径”，不等同于 SP3 发行内核开箱即用、不证明完整内核重建过程，也不能充当 v2 正式性能数据。本次未连接 SP3 实机复验。

**建议验收路径：** 固定可取回的 SP3 kernel-source SRPM/源码及校验和 → 核查源码已有实现与配置 → 有实现则启用配置构建 → 缺少才评估明确的回移集 → 保存 RPM/配置/BTF/构建日志 → 加载测试 → 公平性与性能矩阵。若必须采用自定义内核，应明确其是否被赛题评审接受；在确认前不能将 SP4 替代为主交付环境。

## 5. Towards Agentic OS / SchedCP

**来源 S4：** [arXiv:2509.01245v4 正文](https://arxiv.org/html/2509.01245v4)，[元数据](https://arxiv.org/abs/2509.01245)；访问日期 2026-10-21。作者：Yusheng Zheng、Yanpeng Hu、Wei Zhang、Andi Quinn；v4 日期 2025-09-30。

已确认的方法：SchedCP 将目标推理与策略合成分开，以 MCP 暴露 workload 分析、策略库和执行验证；sched-agent 分为 Observation、Planning、Execution、Learning。验证器用于生成物部署前检查。代码链接为 [eunomia-bpf/schedcp](https://github.com/eunomia-bpf/schedcp)。

借鉴边界：v2 可借鉴“候选建议与受约束执行分离”，但不能把 MCP、Agent 框架、策略生成或部署前验证本身称为原创。v2 第一版生成有界声明式策略；这与论文生成自定义 eBPF 的范围不同。论文所报性能不作为 v2 预期收益或复现结果。本次没有复现实验。

**来源 S5：** [NeurIPS 2025 展示页](https://neurips.cc/virtual/2025/129092)；访问日期 2026-10-21。页面确认该题目属于 **Workshop: ML for Systems** 的 Poster。应写清 workshop 归属，不把它表述为 NeurIPS 主会论文。

## 6. Mixture-of-Schedulers / ASA

**来源 S6：** [arXiv:2511.11628v1 正文](https://arxiv.org/html/2511.11628v1)，[元数据](https://arxiv.org/abs/2511.11628)；访问日期 2026-10-21。作者：Xinbo Wang、Shian Jia、Ziyang Huang、Jing Cao、Mingli Song；题目为 *Mixture-of-Schedulers: An Adaptive Scheduling Agent as a Learned Router for Expert Policies*。本次未核验正式会议发表状态。

已确认的方法：ASA 离线训练 workload 分类器，用时间加权概率投票平滑在线识别，再查询机器相关的策略映射，在线停止/启动调度器并设冷却期；离线流程还校准监测与切换开销。它不等于在线 UCB/Thompson bandit。

设计含义：v2 的“常驻单数据面、切换参数化专家 profile”需要与论文的“切换调度器进程”明确区分；是否更省开销须测量。学习路线优先离线校准与留出 workload 测试，规则、固定最佳策略、离线 learned router、oracle 分开对照。参数化 profile 不应直接宣称复现了完整 MoS。

## 7. 赛题与给定公众号参考

**来源 S7：** [Linux内核可编程与AI驱动的资源控制或成为未来的主流](https://mp.weixin.qq.com/s/ewC8gTMRvm2NO6ywoproAA)；访问日期 2026-10-21。页面标示作者独树居士、日期 2026-03-09，内容讨论内核可编程及 AI 宏观资源调控，并指向 arXiv:2602.09345。

此文可用作方向背景；它属于作者观点，不是 sched_ext 支持矩阵、性能因果证据或赛题规则。没有据此扩张 v2 必交付范围。

**赛题核验限制：** 多组精确题名、英文题名、维护人及 oscomp/educg 定向搜索未获得可确认的新赛题官方详情页。搜到开放原子大赛同名社区题目，属于不同赛事，未用于替代本次规则。用户本次提供的“2026 中西部与华南区域赛、功能挑战、SP3、30/25/25/10/10”文案作为当前需求基线；不能声称已经在线核验其发布日期和规则全文。

## 8. 尚需补齐的实机与规则证据

1. SP3 实机当前 OS/kernel、源码包签名与 SHA256、有效 `.config`、sched_ext kfunc/ops ABI，以及与构建头文件的对应关系。
2. 自定义 SP3 内核的评审接受条件；若必须 stock kernel，需如实说明 CPU sched_ext 主闭环受能力约束，不能以 cgroup 降级冒充完成。
3. SchedPilot 迁移后 Agent/loader 双故障、PMU 不可用、task churn、持续后台负载的公平性和额外开销。
4. 新赛题正式详情页、截止日期和交付包限制。以上均未在本次宣称完成。
