# 实施计划与当前状态

更新：2026-10-21。截止时间和团队可投入时间未知，采用阶段退出条件排期。人日是粗估，不能直接换算为承诺日期。

## 本轮已完成

- [x] 新建独立项目，旧仓库只读参考，保留未提交材料。
- [x] 查阅“开源创新”的相关提交/封口上下文；当前旧仓库版本独立记录，不把历史 release 当最新 HEAD。
- [x] 阅读原稿、审计 EulerPilot 与 SchedPilot，核查外部一手资料。
- [x] 完善 SP3 主交付、控制边界、分类模型、策略发布、恢复及实验方案。
- [x] 用户确认舍弃旧验收界面，优先CLI和实验。
- [x] 提供可校验的 policy/skill/action 设计契约及无特权诊断工具。
- [ ] v2 C/C++ Agent、BPF 调度器、Skill 运行时、CLI 实现。
- [ ] SP3 内核构建、真实加载、功能/故障/性能验收。

当前位置：**P1部分实现但未收口，P2仅advisory切片：控制面、v2 自建 loader（每实例 pin、`eulerpilot` ops）、执行服务持有 link FD（SCM_RIGHTS）、cfg 参数写入、策略校验与 cgroup/pid 目标绑定（cgroup_id、pidfd/starttime，pidfd仅在单次apply绑定中持有，active刷新身份需修复）完成；lease/策略 TTL 自动回退、apply deadline 强制完成；FeatureWindow、advisory 规则路由与 SLO 门控完成；自动apply与性能比较未做，cgroup目标已展开但身份生命周期未收口；A/B/C liveness 与 BACKGROUND pid class_map 已通过。** 2026-10-21 已实现 `ep-executord`（flock 唯一写者、lease/fence、generation、append-only WAL、事务状态机、崩溃 reconcile、幂等）、`ep` CLI、`sched`（loader 一次性 attach 后把 link FD 交给执行服务；每实例 pin；`eulerpilot` ops；崩溃 FD 关闭即 detach；重启回收 pin）与 `sched-ref`（子进程对照），短命 `ep-schedctl`（cfg 参数写入/回读/恢复），以及 `SchedulingPolicy` schema/语义校验与 cgroup v2 / pid TargetBinding（`O_PATH|O_NOFOLLOW`、inode/device/boot_id/cgroup_id、pidfd/starttime、写前 revalidate）。SP3 `ctest` 6/6 通过（unit / integration / integration_sched / integration_fd / integration_lease / integration_ttl）。详见 [P1 骨架报告](p1/P1_SKELETON_REPORT.md)、[SchedSkill 报告](p1/SCHED_SKILL_REPORT.md)、[策略与绑定报告](p1/POLICY_BINDING_REPORT.md) 与 [ABI 冻结](p1/ABI_FREEZE.md)。

P0 证据：SP3 发行版与自编译 `6.6.0-schedpilot-sp3`；源码树 `.config` 与 `/proc/config.gz` 哈希一致；BTF 可用；attach/detach、负载存活、三臂切换均通过；启动参数已加 `psi=1 systemd.unified_cgroup_hierarchy=1`（可回滚），使 cgroup v2/PSI 可用。限制：伪 LLC、共享宿主噪声大；A/B/C 只是 smoke，不能引用为性能。SP4 已启动但未参与 P0。

设计校验日志放在 `evidence/design/`；它不能算任何运行或性能阶段通过。

## 第12轮后审查更正

以 [独立核验与决定](REVIEW_20261021.md) 为当前状态依据：P1仍未满足退出条件。优先修复FD继承/helper无期限、WAL失败回退、active绑定与BPF公平性。当前SLO是CPU/PSI压力门控，缺少真实服务与后台反馈。旧验收日志仅记录当时6项测试与单任务liveness，新版严格验收尚待SP3重跑。

## 阶段与退出门槛

| 阶段 | 粗估工作量 | 工作 | 退出门槛/证据 |
|---|---:|---|---|
| P0 环境 | 2–4 人日 | 获准 SP3 主机/VM；核验内核源码/config/BTF；干净构建 SchedPilot 参考 | SP3 上 attach/detach、workload 存活、参考 A/B/C smoke；kernel/source/build manifest |
| P1 静态闭环 | 5–8 人日 | 精简移植数据面；修复饥饿/心跳回退；执行服务、身份、控制协议；单域WAL/reconcile；静态专家 | 没有双写、TID/实例正确、单域崩溃恢复通过、所有模式 fail-safe、A/B/C 无失速；quiet/后台初测 |
| P2 自适应 | 5–8 人日 | FeatureWindow、规则分类/路由、门控、SLO适配器、发布一致性、CLI | 相位变化可观察；缺指标保守退化；D-rule 与 C 最佳专家同预算比较 |
| P3 复现/性能 | 4–7 人日+机器运行 | pilot冻结、主矩阵/消融/quiet/负结果；原始数据与配对统计 | SP3 冻结构建闭合；有效和失败轮次完整；统计目标/后台底线通过或如实判失败 |
| P4 扩展/恢复 | 3–5 人日 | cgroup Skill；eBPF事件触发链路；跨域saga/WAL扩展；故障注入 | 一条真实hook链路、逆序补偿、跨域重启恢复/冲突报告；不是空接口 |
| P5 交付 | 3–5 人日+必要重跑 | 最终构建再冻结、重跑受后续改动影响的矩阵；SP3手册/镜像、报告、演示、包校验 | 最终二进制与实验溯源一致；独立机器复现；彩排3次；失败清楚标注 |
| P6 可选研究 | 不占前述预算 | 离线学习路由、LLM顾问、额外网络/安全模块、TUI | 未见场景优于规则/固定策略才进入默认交付 |

P3 的实验协议和解析器可以随 P1 旁路开发，正式矩阵必须等 P2 构建冻结。P4 的接口可早定，特权执行修改需串行审查，避免多个 agent 同时改共享 ABI。

## P0 可以直接执行的任务单

1. 确认比赛截止日期和评审内核/VM条件；不替用户发送邮件。
2. 指定一台获准的 SP3 实验环境，记录实际路径和现有改动，先只读盘点。
3. 复核 SchedPilot 历史 SP3 源码配方：`linux-6.6.0-145.3.34.165.oe2403sp3` 与 config 差异；来源缺失时先补来源，不盲目回移补丁。
4. 在新目录构建，记录 libbpf、scx helper/header、BTF、编译器版本及对象哈希；不覆盖旧 loader/daemon。
5. 用隔离临时 pin/cgroup 路径执行最小 attach/detach 和 Redis smoke；先检查已有调度器与所有权。
6. 跑短 A/B/C，记录 quiet 和 BG 吞吐；此处检查可运行性，结果不得写入正式性能表。
7. 形成 P0 报告：PASS/FAIL/UNSUPPORTED、阻塞条件、实际 ABI 及下一阶段可用资产。

### P0 完成映射（2026-10-21）

| 任务 | 状态 | 证据 |
|---|---|---|
| 1. 截止日期/评审条件 | 待用户补充 | 不影响 P0 技术门槛 |
| 2. 指定 SP3 环境 | 完成 | <JUMP-HOST> `schedpilot-sp3`，`<SP3-IP>`，8 vCPU/16 GiB |
| 3. 复核 SP3 源码配方 | 完成 | `kernel-source-6.6.0-145.3.34.165.oe2403sp3`，config 哈希一致 |
| 4. 新目录构建与哈希 | 完成 | `/root/p0/schedpilot-0aeca0c`，loader `3da28c29…`、daemon `f1f60b70…`、BPF `ba1c2a63…` |
| 5. attach/detach + Redis smoke | 完成 | 6 轮 B/C attach 全部 `enabled`，结束回 `disabled` |
| 6. 短 A/B/C | 完成（smoke） | quiet 与 noisy 各 3 轮，0 无效；数字不作性能结论 |
| 7. P0 报告/manifest | 完成 | `docs/p0/SP3_ENV_REPORT.md`、`evidence/p0/sp3-reference-manifest.json` |

未完成的 P0 子项只有“用户提供截止日期/评审内核政策”。BG 吞吐未测（参考干扰脚本不导出工作单元），
必须在 P1/P3 换成能导出进度的压力源；不得用本次 smoke 冒充公平性结论。

### P1 进展（2026-10-21）

| 项 | 状态 | 证据 |
|---|---|---|
| 控制面 ABI 子集冻结 | 完成 | `docs/p1/ABI_FREEZE.md` |
| `ep-executord` 唯一写者/lease/generation/WAL/reconcile | 完成 | `src/executor/`、`include/ep/` |
| `ep` CLI（status/lease/apply/rollback/reconcile） | 完成 | `src/cli/main.cpp` |
| `SchedSkill`（sched-ref） | 完成（v2 自建 loader，子进程） | SP3 真实 attach/detach/崩溃回收，见 `docs/p1/SCHED_SKILL_REPORT.md` |
| 单元 + 端到端 + 真实内核测试（SP3） | 完成 | `ctest` 6/6 |
| SP3 一键验收 | 旧版历史通过；新版待重跑 | `scripts/accept_sp3.sh`：ctest + A/B/C liveness + 哈希 + 收尾状态；证据 `evidence/acceptance-sp3-20261021.txt` |
| `SchedulingPolicy` schema/语义校验 | 完成 | 字段/范围/枚举/未知字段 + 心跳/TTL 语义，见 `docs/p1/POLICY_BINDING_REPORT.md` |
| cgroup/pid TargetBinding | 完成（cgroup_id + 持有 pidfd） | cgroup: inode/device/boot_id/cgroup_id；pid: 持有 pidfd + starttime；写前 revalidate |
| v2 自有 loader / pin 命名空间 | 完成（FD 传递 + 每实例路径） | `sched/` 构建 `ep-sched-loader`；loader 一次性 attach 后交 link FD；pin `/sys/fs/bpf/eulerpilot-v2/<instance>`，ops `eulerpilot` |
| 执行服务持有 link FD | 完成（SCM_RIGHTS） | `sched` 模式执行服务持有 link FD；崩溃 FD 关闭即 detach；`sched-ref` 保留子进程对照 |
| 租约过期自动回退 | 完成 | 500ms 监控；过期后自动 rollback 到 fair、清 pin、置 degraded/last_fallback；失败记 RECOVERY_REQUIRED |
| 策略 TTL 自动回退 | 完成 | `safety.policy_ttl_ms` 到期同样自动 rollback；与 lease 共用监控/日志路径 |
| apply deadline 强制 | 完成 | 入口/apply 后/verify 后检查；超时补偿，不提交；rollback 不中断 |
| A/B/C 无失速（liveness） | 完成 | `experiments/abc_nostall.sh`：fair/basic/class 三臂 utime 均推进、状态干净；不是性能比较 |
| `spec.parameters` 写入 cfg map | 完成 | 短命 `ep-schedctl` 写前快照/写后回读，rollback/recover 先恢复 |
| link FD CLOEXEC | 完成 | MSG_CMSG_CLOEXEC + fcntl；integration_fd 校验 fdinfo |
| helper 超时 | 完成 | ctrl_timeout_ms=2000；整轮 500ms 绝对预算（min lease/TTL）、失败即停；EOF 同期限；进程组 kill/reap |
| WAL 失败恢复 | 部分完成 | append 失败即 broken/回退末条；emergency detach；DEGRADED/AUDIT_PENDING；active 影响前建立 |
| WAL 重启对账 | 未完成 | broken journal 重启 repair/reconcile、state.json 故障注入 |
| active 绑定身份贯穿 | 部分完成 | active 持有 pidfd/cgroup dir FD；refresh 逐绑定重验；成员迁出 class-del；BPF TGID 窗口仍存 |
| BPF 有效策略/公平性 | 部分完成（首版未达标） | 统一 effective_policy、per-CPU 运行时间预算、aging、class-del；SP3 压力 LAT 31%<60% 预算、throttle=0，class_map 命中/切片记账需诊断；不得作为性能结论 |
| P2 FeatureWindow（主机切片） | 完成 | CPU/load/PSI；valid/source/samples/age/missing；`feature.snapshot` + `ep features` |
| P2 规则分类/路由 | 完成（advisory） | 确定性阈值 + 缺指标退化 + 5s 滞回；`route.decide`/`ep route`；不自动写调度器 |
| P2 SLO 门控 | 完成（advisory） | 缺样本/过期/缺 PSI/PSI 高压拒绝；safe-basic 可退化；`slo.evaluate`/`ep slo` |
| P2 非人工来源 SLO 否决 | 完成 | rule/offline/llm 的 apply 先过 SloGate；拒绝 `SLO_DENIED` 且无副作用；human 绕过 |
| P2 class_map（BACKGROUND pid + cgroup） | 完成（部分） | pid 直接写；cgroup 扫描成员展开（≤256），2s 刷新；FOREGROUND/NEUTRAL 不映射 |
| P2 自动 apply 路由结果 | 未开始 | 必须显式 policy + lease，不能成为第二写者 |
| cgroup Skill、eBPF 事件链 | 未开始 | P4 范围 |

P1 退出门槛仍未满足：还需要其余模式 fail-safe、加载场景的 SLO/无失速；
单域崩溃恢复和 A/B/C liveness 已完成，但 A/B/C 性能比较尚未做。当前是“控制面 +
v2 自建数据面（FD 传递 link）+ 用户态安全边界”，不可当作 P1 完成。
`sched-ref` 子进程模式仍保留作对照，且必须与 `schedpilotd` 互斥；生产路径是 `sched` 的 FD 传递模式。

## 实现工作包

| 工作包 | 主要写入范围 | 可并行关系 | 验证重点 |
|---|---|---|---|
| 数据面 | `sched/`、共享 ABI | ABI 冻结后与用户态并行 | 饥饿、slice记账、stale参数、队列切换、verifier |
| 特征/路由 | `src/agent/` | 可用录制 FeatureWindow 离线开发 | 缺失/相位/置信度、确定性、不把BG当行为类别 |
| 执行/恢复 | `src/executor/`、`src/skills/` | 先与数据面协调单一写者协议 | fencing、幂等、WAL顺序、真实回滚失败 |
| 实验/统计 | `bench/`、`experiments/` | 可先做录制数据解析 | 单位、latency直方图、资源一致、失败分母 |
| 文档/交付 | `docs/` | 与实现并行，依据实测更新 | 不把计划能力/旧结果当已实现 |

每个工作包都需列出源文件迁移清单、测试范围和已知限制。不能靠“旧项目通过过”豁免新对象的验证。

## 压缩版与停止条件

若时间紧：只做 Redis + Nginx、三专家规则版、cgroup + 一个事件hook、CLI与完整SP3证据；不做学习/LLM/Web/完整K8s。最低版仍需真实感知→策略调整、sched_ext闭环、可扩展接口和扩展证明。

- SP3 获准内核未跑通：停止扩展功能，先解决环境，不以SP4降级交付掩盖。
- 静态 C 不优于强基线或造成后台饥饿：先改数据面和公平性，暂停学习路由。
- D-rule 不优于 C：允许默认交付 C/规则门控，报告自适应的边界；不通过删样本制造优势。
- quiet 开销超预算：降低观测成本、考虑稳定 fair idle 状态，再做重跑。
- 正式构建之后改实现：受影响的矩阵必须重跑，不能沿用原二进制证据。

## 需要用户补充但不阻塞当前方案的信息

提交/答辩日期；团队人数与时间；可用SP3/物理机器及权限；评审是否接受自定义内核/VM；最关心的服务与SLO。下一轮优先据此安排P0，当前不假设可任意重启现有服务器或改宿主内核。
