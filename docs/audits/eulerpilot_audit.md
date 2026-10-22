# EulerPilot 旧项目审计与 v2 取舍

核查日期：2026-10-21。范围：本地源码、设计稿和仓库内正式实验汇总；未运行旧项目、未加载 BPF、未写远端，不能据此声称运行时测试通过。当前源码 HEAD 为 `15e5a21d9ec9d53a94b4aa35812b33dae67ac6a3`；历史正式实验绑定的是另一个提交 `7a99d87048f4f2040377354bfe0ce21401664642`，以下机制分析与历史测量必须区分。

## 结论

EulerPilot 最值得继承的是目标解析、多 Skill 生命周期、可读审计、跨域补偿和实验留痕；旧调度器不适合作为 v2 性能基座。对新赛题，SP3 上完成 sched_ext 闭环是交付条件；SP4 数字和 cgroup 降级都不能替代这一条件。CLI 和可复现实验应成为主验收路径，旧前端暂不迁移。

## 历史性能：确认了什么

来源：`D:\code\Ubuntu\EulerPilot\reports\v6_formal_experiment_suite_20260726.md`，行号如下。本文不将汇总文件当作原始样本。下文源码路径均以 `D:\code\Ubuntu\EulerPilot` 为根。

| 场景 | RPS 改变均值及 95% CI | p99 改善均值及 95% CI | 证据位置 |
|---|---|---|---|
| Redis GET / scx_normal | +3.40% [-3.28, 10.08] | -37.75% [-138.61, 63.11] | 报告 41–48 行 |
| Redis GET / scx_always_active | -62.62% [-81.57, -43.67] | -220.20% [-392.92, -47.48] | 报告 47 行 |
| Redis INCR / scx_normal | +17.01% [8.41, 25.61] | +48.54% [11.58, 85.50] | 报告 50–57 行 |
| Redis SET / scx_normal | -6.21% [-11.97, -0.45] | -3.54% [-18.31, 11.24] | 报告 68–75 行 |
| Nginx / scx_normal | -0.13% [-1.81, 1.55] | -11.86% [-18.87, -4.86] | 报告 77–84 行 |
| Nginx / scx_always_active | -19.76% [-24.75, -14.78] | -68.93% [-99.97, -37.89] | 报告 83 行 |

这里的 **p99 改善为正才表示变好**；负值表示延迟变差。不能把 GET 的 `-37.75% improvement` 写成“p99 下降 37.75%”，也不能把 Nginx 的 -11.86% 写成延迟改善。GET/scx_normal 的吞吐和延迟 CI 都跨零，不能宣传稳定收益。

报告 17–18 行给出 quiet 平均吞吐：Redis 36005.720 → 26687.541，约 -25.88%；Nginx 21169.683 → 19879.530，约 -6.10%。这是平均值比值的描述，未给出 quiet 配对 CI，不能当作与上表完全相同的估计量。

报告 36–37 行明确 noisy_default 已修正为 `cpu.weight=100, cpu.max=max`，采用逐轮配对 t 区间。报告 7–11 行说明原始 JSON/CSV/trace/log 位于远端 `/root/eulerpilot-runs/...`，本次未读取它们。报告 25–29 行也主动限定：mixed-adaptive 是功能闭环，非净性能证明；用户态 Agent 开销约 0.45–0.55% 单核、RSS 7.4–7.5 MB，**不能据此证明整个 SCX 数据面开销低**。

## 数据面机制风险：不要误写成已定位根因

| 源码事实 | 可能影响 | v2 应对 |
|---|---|---|
| `sched/scx_eulerpilot.bpf.c:349` 优先消费 LAT，成功即返回；353 行后才检查 BG 饥饿保护 | LAT 持续非空时 BG guard 没有执行机会；SHARED 在 371 行才消费，也可能长期等待 | 跨类预算/老化需覆盖全部类；测试持续 LAT + UNKNOWN/BG，不只测前景 QPS |
| 同文件 304–313 行：类 DSQ FIFO，SHARED 才走 vtime 排序 | 类内权重和跨类公平性不能由共享队列的 vtime 自动提供 | 明确每类可获得的 CPU 服务预算并记录实际服务量 |
| 同文件 279–288 行：空闲 CPU 直接插入 local DSQ，slice 是 `SCX_SLICE_DFL` | 可能绕开类 slice；空闲/竞争阶段执行机制不同 | 对直接本地路径单独计数，保证记账使用实际授予 slice |
| 同文件 388–401 行：stopping 重新按当前 gate/class 获取 slice，再减剩余 slice | 执行期间切换 gate 或分类会使记账基准变化 | 在 task storage 保存实际 dispatch slice，或使用可靠运行时间增量 |
| 同文件 110–123 行：class_map 仅按 TGID 查 `u32 class` | 内核执行路径未校验进程启动身份和策略期限 | 键/值绑定启动时间或 cgroup 身份、epoch、TTL |
| 同文件 126–140 行：只检查 state 枚举，不检查 updated_at_ns | 旧实现不能凭 map 中有时间戳宣称心跳超时兜底 | 新数据面显式实现 lease 过期路径，故障注入验证 |

这些均来自当前代码静态审阅。历史实验构建不同；没有 CPU profile、DSQ 等待分布、竞争计数和相同环境消融，不能认定任一项就是历史 -62.62% 的原因。v2 应将它们列成可证伪假设，先测单 shared/basic 开销，再逐项启用分类、抢占、LLC、反馈。

## 控制面：保留思想，重写边界

| 资产 | 证据路径/行号 | 价值与迁移方式 |
|---|---|---|
| Skill 生命周期接口 | `agent/include/skill.hpp:11–39` | configure/probe/init/start/snapshot/rollback/stop；保留职责，新增版本化输入输出 schema、超时、幂等键、错误分类 |
| Skill 注册/依赖拓扑 | `agent/src/skill_registry.cpp:9–37`；`agent/src/skill_manager.cpp:49–160` | 拒绝未知/重复 Skill，稳定 CLI 输出，依赖排序；保留行为与单测用例，避免直接迁移字符串扁平配置 |
| TargetResolver | `agent/include/target_resolver.hpp:9–76`；`agent/src/target_resolver.cpp:790–822,997–1068` | PID/cgroup/container/Pod/netdev 有统一结构、实验命名空间和 hostNetwork 约束；首版仅迁移 PID+cgroup，容器/Pod 为后续适配器 |
| 运行时身份增量 | `agent/src/runtime.cpp:232–254` | 优先 BPF start_boottime，fallback generation+计数重置；有价值，但必须贯穿执行链 |
| 跨域补偿流程 | `agent/src/builtin_skills/policy_engine.cpp:685–805` | 单事务上下文、逆序恢复、避免覆盖外部新值；抽出小型事务协调器 |
| 审计关联 | 同文件 695–704 行；`agent/src/action_journal.cpp:40–83` | transaction_id/trigger_event_id/policy_id 跨 Skill 关联；保留字段，改为写前日志与重启恢复 |
| network/security/resource 资产 | `agent/src/builtin_skills/{network_policy,network_qos,network_xdp,security_policy,resource_control}.cpp` | 只选择 1 条最小扩展闭环，逐个能力 probe，不做首版 K8s/所有 hook 同时迁移 |

### 双写与所有权

`sched/scx_eulerpilot.c:217–241` CLI 读取后自增 generation、直接写 gate map；`agent/src/executors.cpp:1483–1507` Agent 也能直接写。二者没有基于 generation 的原子比较更新；generation 是证据编号，不能自动形成互斥。v2 应由一个特权 executor 持有写权限/进程锁，Agent、CLI、Skill 都发送有界请求；不允许旧 daemon 与新 Agent 同时调同一 map。把 owner 放在普通可写 map 中也不能阻止其他 root 写者，需明确这不是对抗 root 的安全机制。

### 身份绑定

`agent/src/executors.cpp:1454–1475` 将感知层身份降成 TGID→class，清理条件仅是 TGID 不活跃；感知层有启动时间不能证明执行层不存在 PID 重用风险。TargetIdentity 的 `pid` 和 `cgroup_id` 也未包含 boot_id/start_time/保留目录 FD。v2 应在 prepare/apply/verify/compensate 时重新核验启动身份、cgroup inode/保留 FD、netns 与 ifindex，任务退出/目标重建则拒绝陈旧请求。

### 事务与恢复

- `policy_engine.cpp:618–648` 先读取旧值、写控制文件、标记 applied、回读验证；`719–725` 才写 journal，SIGKILL 可发生在副作用与 journal 之间。
- `action_journal.cpp:40–83` 用 ofstream append，成功路径没有 flush/fsync 后的错误检查；本函数只记录，不提供崩溃恢复。需要 PREPARED/COMMITTING/APPLIED/COMPENSATING/COMPENSATED/RECONCILE_REQUIRED 的持久化状态和恢复流程。
- `policy_engine.cpp:752–805` 逆序恢复且拒绝覆盖外部变更，这是值得保留的保护；但补偿可能失败，所以不是内核级跨域原子事务。
- `policy_engine.cpp:235–240` 调用 void restore 后无条件置 `rolled-back` 并 return true，即使 769/799 行记录失败。v2 必须把部分恢复或冲突显式输出，不能把“调用 rollback”记作“恢复成功”。
- `policy_engine.cpp:655–680,777–787` TC 路径拒绝覆盖已有 root qdisc，仅当当前 qdisc 与自己应用后完全一致时删除；这是受限实验范围中的补偿，不能宣传任意现存网络配置可自动还原。

### 安全边界

首版只管理显式授权的实验 cgroup/veth，不对整机所有任务自主限流；BG 是业务治理角色，不能从 CPU-heavy 特征自动推导“可牺牲任务”。仅名称/comm 不构成授权。LLM/策略 YAML 是不可信建议输入，不得携带任意 shell、文件路径或 BPF 代码；所有动作必须落在版本化 schema、能力探测、目标授权和固定参数范围内。

`policy_engine.cpp:365–375` 是字符串前缀 + 控制文件白名单，不等于路径解析后的资源隔离；v2 需用已授权根目录 FD、规范化/禁止穿越、目标身份重查。现有代码未在本次实际运行，不在本文断言已完成路径攻击验证。

## 原设计稿必须修改的地方

1. **交付要求偏移**：7、20、372–400 行把“SP4 才完整支持”“赛题文案未更新”当结论，没有足够外部证据；已有 SP4 自编译内核成功并不说明 SP4 stock 内核支持，更不证明 SP3 无法支持。SP3 用户态+明确内核包/补丁/配置必须列 P0；若自定义内核受限，要明确这是待确认合规条件，不能自行换环境。
2. **把选或生成读成选且生成**：207–249 行认为只做动态选择不够，超出用户提供的“选择或生成”；DSL 有工程价值，但不是替代基本闭环的硬门槛。首版动态选可验证专家策略足以覆盖这部分要求，生成作为增量。
3. **创新性夸大**：190 行以“直接对应参考论文”证明创新不成立；应区分复现/借鉴，与本项目新增的跨域约束、真实反馈、保底机制、可验证的路由收益。
4. **安全门控因果跳跃**：205 行由旧 always_active 负收益推出默认 BASIC 必然优，是设计假设；BASIC 本身 quiet 开销约 -25.9%，需先测并设置默认 fair/observe 基线，不能把 shared DSQ 等同默认调度器。
5. **混合架构含糊**：“Mixture-of-Expert-Schedulers”若只是一个 BPF 调度器中的参数 profile，应称策略专家，不应暗示热切换多个实际 scx 调度器已经实现。
6. **离线仿真边界**：trace replay 可检查确定性、参数边界、控制抖动，不能可靠预测闭环改变后的 wakeup/cache/竞争，也不能替代真实收益验证。
7. **跨域整体回滚承诺过强**：已有代码是可失败的补偿恢复；memory.max 可能触发 OOM、网络 drop 已发生的数据损失不能逆转。首版只做可恢复配置变更，演示失败输出及人工处置。
8. **DSL 命名/语义**：234 行 `max_switch_interval_s` 的命名与“最小切换间隔”相反，应为 `min_dwell_ms` 等；全局调度参数与每目标约束必须分开，多个目标策略发生冲突要有仲裁。
9. **验收不能预置成功**：性能阈值是目标，现场 `make verify` 不应在硬件噪声导致未达阈值时伪造通过；功能 PASS、统计结论、性能目标分别展示。8 周/4–5 周排期是工作量估算，并非获知的赛期。

## 建议的首批迁移顺序

1. 提炼 TargetIdentity、Skill/Tool schema、能力 probe、只读 CLI 与原始事件格式；新项目不复制旧前端与历史生成物。
2. 用 SchedPilot 数据面在目标 SP3 内核建立可卸载/可恢复的固定策略闭环；旧 Euler 调度器只作为可选历史对照，不在关键路径维护。
3. 固定策略对默认 fair、等预算 cgroup、静态专家的性能；暴露前景收益、背景进度、总工作量和 quiet 税。
4. 接规则路由器，增加真实 SLO 反馈、目标身份与单写者；规则稳定后再评估学习/LLM。
5. 补偿协调器+一个 cgroup/TC 扩展，测试中途失败、SIGKILL、重启恢复和外部变更冲突。

尚未完成：原始远端历史样本重算、当前构建运行测试、openEuler SP3 实机验证、任何 v2 性能验证。本文仅是静态审计与迁移建议。
