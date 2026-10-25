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

当前位置：**P1部分实现但未收口，P2仅advisory切片：控制面、v2 自建 loader（每实例 pin、`eulerpilot` ops）、执行服务持有 link FD（SCM_RIGHTS）、cfg 参数写入、策略校验与 cgroup/pid 目标绑定（cgroup_id、pidfd/starttime，pidfd仅在单次apply绑定中持有，active刷新身份需修复）完成；lease/策略 TTL 自动回退、apply deadline 强制完成；FeatureWindow、advisory 规则路由与 SLO 门控完成；自动apply与性能比较未做，cgroup目标已展开但身份生命周期未收口；A/B/C liveness 与 BACKGROUND pid class_map 已通过；数据面 LAT 预算在单 CPU 封闭竞争中已证实"份额由片长比决定、lat_budget_pct 只封顶"，改用片长配额 + 真实墙钟记账后 60% 预算实测 LAT 57.9–58.5%（3 轮）且 throttle 每轮触发；`accept_sp3.sh` 的 `integration_sched` 曾出现的 `enable_seq changed since attach` 竞态已按内核发布顺序修正并连跑 6 轮通过（见下文两行）**。 2026-10-21 已实现 `ep-executord`（flock 唯一写者、lease/fence、generation、append-only WAL、事务状态机、崩溃 reconcile、幂等）、`ep` CLI、`sched`（loader 一次性 attach 后把 link FD 交给执行服务；每实例 pin；`eulerpilot` ops；崩溃 FD 关闭即 detach；重启回收 pin）与 `sched-ref`（子进程对照），短命 `ep-schedctl`（cfg 参数写入/回读/恢复），以及 `SchedulingPolicy` schema/语义校验与 cgroup v2 / pid TargetBinding（`O_PATH|O_NOFOLLOW`、inode/device/boot_id/cgroup_id、pidfd/starttime、写前 revalidate）。SP3 `ctest` 6/6 通过（unit / integration / integration_sched / integration_fd / integration_lease / integration_ttl）。详见 [P1 骨架报告](p1/P1_SKELETON_REPORT.md)、[SchedSkill 报告](p1/SCHED_SKILL_REPORT.md)、[策略与绑定报告](p1/POLICY_BINDING_REPORT.md) 与 [ABI 冻结](p1/ABI_FREEZE.md)。

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
| SP3 一键验收 | 完成（最新一轮 r21） | `scripts/accept_sp3.sh`：ctest + A/B/C liveness + 哈希 + 收尾状态；`scripts/accept_sp3_rounds.sh`（本轮起新增的轮次驱动，命名与归档不再靠 sed 改）连跑 6 轮 rc=0、0 SKIP（`evidence/acceptance-sp3-20261023-r21/round1..6/`、`acceptance-sp3-strict-20261023-r21-round1..6.txt`、`acceptance-sp3-20261023-r21-driver.log`）；r21 的 `ep-sched-loader 45bdc3c0…` 是 runnable-task-stall 修复后的数据面构建；历史轮次 r7c/r8..r14 与 r15..r20，失败轮 r7a/r7b 也保留 |
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
| BPF 有效策略/公平性 | 部分完成（配额与记账已实跑验证） | 统一 effective_policy、per-CPU 预算、aging、class-del；份额实际由片长比决定，故按 `lat_slice=comp_slice×pct/(100−pct)` 表达预算配额；stopping() 改为真实墙钟时长记账后 throttle 才生效。SP3 单 CPU 竞争下 60% 预算实测 LAT 57.9–58.5%（3 轮）、throttle 每轮触发、BG 10.1–10.5%、最大间隔 ≤55ms。旧"31%/throttle=0"与 HIT=14 均为读数或模型误解，见 `evidence/p1/fairness-budget-20261022.txt`；逻辑正确性，不作性能结论 |
| accept_sp3 下 enable_seq 竞态 | 已按内核真实语义修正 | 上一版把原因归为“第三方 bump / CPU 热插拔”是错的，已在 `evidence/p1/enable-seq-verify-20261022.txt` 第 6 节更正。真实原因是 `scx_enable()` 的发布顺序：state 先可读 → pr_info → 同步 uevent → 解锁 → **最后**才 `atomic_long_inc(&scx_enable_seq)`（`kernel/sched/ext.c:5503–5518`，全文唯一递增点；热插拔走独立的 `scx_hotplug_seq`），apply 看到 enabled 就读计数器，因此读到递增前的旧值。现以 snapshot 的 pre-enable 值为基准，等计数器发布并连续两次一致后再记录（`wait_enable_seq_stable`），`enable_seq` 恢复等值判定并入 `presence_ok`，`verify` 与 `liveness` 共用同一份判断。真内核 `ctest` 两轮 6/6、`accept_sp3.sh` 6 轮通过。见 `evidence/p1/enable-seq-publish-20261022.txt` |
| 生效后被顶替无法发现 | 已实现（unit 覆盖） | 新增 `Skill::liveness`，监控每 1000ms 探测、连续 3 次 miss（≈3s）才复用既有 `auto_fallback_locked`；瞬态单次读取不误撤已测。同名实例顶替靠 `enable_seq` 等值发现——link FD 这条路经源码确认不可行（6.6 的 `bpf_struct_ops_link` 无可查询的 attached 状态，`.release` 为空且无 `.detach`）。边界：`fd_passing` 的在位性覆盖边界（2026-10-23 明确）：正分支（执行服务持有 link FD 时逐项校验）由 `tests/integration_fd.sh` 在**真内核集成层**覆盖——它用 `--skill sched` apply 并断言两次 `VERIFIED`，而 `verify` 走的正是同一个 `presence_ok`；负分支（`link_fd_ < 0` 报 `executor does not hold the struct_ops link fd`）在 unit 层**不可达**，因为 `link_fd_` 只在 apply 里由 SCM_RIGHTS 收到，没有合法的测试注入口，所以该分支记为 UNSUPPORTED-at-unit，不为它加产品侧后门；第三方顶替已实测——已有调度器生效时，**第二次 struct_ops 注册被内核以 EBUSY 拒绝**（参考实现 `scx_schedpilot.c:376`、v2 自己的第二个 loader `sched/loader.c:468`），运行中的调度器、`root/ops` 与 `enable_seq` 都不变，故“存活期间被顶替”不可构造，顶替检测停留在 unit 级证据；`enable_seq` 等值判定下调为快速顺序接管的采样盲区兜底（`evidence/p1/takeover-thirdparty-20261022.txt`）。实验同时暴露并修复启动期原因丢失：新增 `startup_degraded_reason` 贯通日志/status/apply 拒绝 detail，`test_startup_degraded_reason_surfaced` 覆盖。见 `evidence/p1/liveness-watch-20261022.txt`、`evidence/p1/takeover-20261022.txt` |
| 持续服务负载下被内核摘除（runnable task stall） | 已定位并修复（真机验证） | 旗舰 pilot 的 6 个挂载块全部在约 46s 被 `scx_watchdog_workfn` 以 `runnable task stall` 摘除，受害者一律是 per-CPU kworker；配料分离证明触发条件是服务负载本身（不加载/仅后台 90s 均 0 次），`stats` 的 `enq_comp == dsp_comp` 否掉"DSQ 没人排空"，指向内核亲和性改投：绑定任务被别的 CPU 从共享 DSQ 拉走时 `move_task_between_dsqs()` 不允许 → 改投全局 DSQ → 只有它自己那个 idle CPU 能取回而无人唤醒。第一个假设（给非唤醒 enqueue 补 idle 唤醒）**未修好**，作为文档要求的正确改动保留；真正修复是 `nr_cpus_allowed == 1` 直投自己 CPU 的 local DSQ + `SCX_KICK_IDLE`，新计数 `enq_pinned_local`。验证 `269/269 poll enabled`、dmesg 0 条新 stall、`degraded=False`、generation 未变；修复前同机同负载 6/6 失败。执行服务侧无需改动：3s 内检测、补偿式回退、pin 清理、原因进 `last_fallback`。**边界**：不能声称 stall 类缺陷已穷尽，且该轮 `enq_lat/enq_cache/enq_bg` 全为 0（所有任务落默认类），LAT/CACHE/BG 三条类路由未经此压测。BPF 重建流程脚本化为 `scripts/build_bpf_sp3.sh`。见 `evidence/p1/stall-runnable-20261023.txt` |
| P2 FeatureWindow（主机切片） | 完成 | CPU/load/PSI；valid/source/samples/age/missing；`feature.snapshot` + `ep features` |
| P2 规则分类/路由 | 完成（advisory） | 确定性阈值 + 缺指标退化 + 5s 滞回；`route.decide`/`ep route`；不自动写调度器 |
| P2 SLO 门控 | 完成（advisory） | 缺样本/过期/缺 PSI/PSI 高压拒绝；safe-basic 可退化；`slo.evaluate`/`ep slo` |
| P2 非人工来源 SLO 否决 | 完成 | rule/offline/llm 的 apply 先过 SloGate；拒绝 `SLO_DENIED` 且无副作用；human 绕过 |
| P2 class_map（BACKGROUND pid + cgroup） | 完成（部分） | pid 直接写；cgroup 扫描成员展开（≤256），2s 刷新；FOREGROUND/NEUTRAL 不映射 |
| P2 自动 apply（最小切片） | 已实现并在 SP3 实测 | 新增纯函数 `AutoPilot` + 执行服务内部接线，`--auto-apply/--auto-policy/--auto-owner` 显式开启，baseline 校验失败即拒绝启动；只能改 `spec.expert` 且在三个已验证档位内，targets/safety 一律沿用人工 baseline，source 记为 `rule-template` 因必过 SLO 门控；只在把租约显式交给它时动作（`ep lease --owner` 新增），他人取得租约即停手；切换走 rollback→apply。SP3 实测三条属性成立（交接前 hold、PSI 越过 20 后切到 latency-balanced、reclaim 后 generation 不变）。见 `evidence/p2/auto-apply-pilot-20261022.txt` |
| 切换代价量化 | 完成 | `experiments/auto_apply_downtime_sp3.sh` 实测：每次切换空窗 665.0ms（升载）与 303.1ms（回落），`enable_seq` 移动 3 次与 3 次 attach 对应；同负载对照下空窗内 1ms 周期唤醒 p99 1.2–2.0ms → 13.2–16.1ms、max 2.2–3.0ms → 20.2–23.1ms、每探针 1–4 次 ≥20ms；撤载后的空窗无可测毛刺。120s 内自动来回切 2 次，`--auto-min-interval-ms 3000` 挡不住阶跃负载 ⇒ 自动 apply 不能默认开启。见 `evidence/p2/switch-downtime-20261022.txt` |
| 切换抑制（dwell / 候选保持 / 预算） | 已实现并在 SP3 逐门关实测 | `src/core/autopilot.cpp` 判定分三层且顺序即契约：权限门→证据门→抑制门（`dwell_ms` 60000、`candidate_hold_ms` 15000、`min_interval_ms`、`budget_max_switches` 2 / `budget_window_ms` 600000）。失败的动作照样消耗预算（回滚已真的撕开一段空窗）；每次成功切换在日志与 `policy.status.auto_apply` 报出预算占用与 `projected_unprotected_ms`。unit 20 条 decide 断言（含 5 条抑制与"权限优先于抑制"）；SP3 三次隔离重放逐一证明每个门都能独立成为决定门：dwell 挡到 `in place 59755ms`/60000 才切、candidate_hold 挡到 38161ms/40000（该轮 dwell 行数 0）、预算在 100s 观察窗内 65 次拒绝回落（`switches=1`）。回归：全量重建 0 警告、`ctest` 6/6、`accept_sp3.sh` 6 轮全 rc=0（`evidence/acceptance-sp3-20261022-r14/`）。见 `evidence/p2/autopilot-suppression-20261022.txt`（第 6 节更正了本文件上半部分两处错误） |
| 服务级 SLO 反馈（加载场景） | 已实现并在 SP3 闭环 | 新增观测契约 `ServiceObservation` 与 `service.observe` RPC（CLI `ep observe`）：不取租约、不写调度器、不推进 generation，**只能让门更严**。`SloGate` 增加服务半区，阈值只取生效策略 `spec.safety` 的声明值（`min_bg_work_units_per_sec`/`max_bg_wait_ms`），并按"该档此前被实测判过退化 → 观测过期 → 进度下限 → 等待上限"排在宿主特征之前；缺席时报 `not evaluated` 而非通过。切换真正换了 expert 时武装切换后判定 `evaluate_switch_outcome()`（进度比/绝对下限/等待上限/前向 p99），并引入 `--auto-outcome-windows K`（默认 2）：单窗违规只记 suspected，连续 K 窗才写入 `adverse_experts`。`policy.status.service` 报出测量值、声明阈值、按档归属的证据与待判定窗口数。SP3 实测：租约未交时基线窗口内确实不动、切换武装判定、两窗确认、撤载回落到未被禁的档（不连坐）、饥饿注入后 `BG_PROGRESS_FLOOR` 使自动通道停止切换。**量化限制**：同状态连测 8 窗 p99 极差/均值 46%（bg 进度仅 5.4%），而声明的 `quiet_max_regression_pct` 是 5% ⇒ 进度类约束在此精度可用、p99 类约束目前不可用于自动判定。见 `evidence/p2/service-slo-20261022.txt`（含 run1..run5，其中 run2 单窗误判与 run4 观测断供即停是设计依据） |
| 服务观测分辨率口径与护栏 | 已实测确立并实现 | 先用 `experiments/slo_noise_floor_sp3.sh` 量信号本身（同状态重复取窗 5/15/30s×5）：**p99 CV 3.8–20%，且 30s 窗比 15s 窗更吵**（长窗采到的是变化后的世界）；**背景进度相邻窗 ≤1%**。据此把在线口径写进 `docs/EXPERIMENT_PROTOCOL.md` §5.1（窗口 15s、观测常驻、比较类判定先声明分辨率、绝对类不受限、前向延迟暂只用绝对量）。产品侧 `evaluate_switch_outcome` 接 `ServiceNoise`：策略要求的敏感度不大于实测抖动时判定**不执行**并明报 `comparative check not run`（既不判违规也不判通过）；抖动只由同档**非 pending** 窗口算出，相差 >4× 的相邻窗视为状态变化而非抖动（run7 的 90.3% 假抖动逼出来的）；判定顺序改为绝对量优先。真机 run8：`p99 噪声 39.6% ⇒ 不判`、无人被冤枉、饥饿注入仍 `BG_PROGRESS_FLOOR` 拒动。另记一个运维事实：靠人持有租约冻结自动通道必须显式 `--ttl-ms`，CLI 默认 30s 租约会在测量中途到期（run6）。见 `evidence/p2/slo-noise-floor-20261022/`、`evidence/p2/service-slo-20261022.txt` §8 |
| 前向延迟绝对上限 + 常驻观测 | 已实现并在 SP3 实测 | 策略新增可选 `spec.safety.max_foreground_p99_ms`（1..60000ms，schema 可选键、越界拒绝不钳制）；SloGate 的 `foreground_ceiling` 检查**不受分辨率护栏豁免**（单窗事实），且不写入 `adverse_experts` ⇒ 超限消失即放行、无需人工清除。真机：策略声明 1ms，加载后实测 p99 1.545/1.282/1.201ms ⇒ `FOREGROUND_P99_EXCEEDED`，同期宿主特征全部合格、观测未过期（age 1002ms），路由确实想升档（PSI 22.1），自动通道全程 **0 次切换**；5 轮 15s 常驻观测从未触发 `STALE`。生产者独立成 `experiments/service_observer_sp3.sh`（切换中跳过轮次、不贴错标签）。一处读序假象与未演示项（撤载后重新放行只由 unit 与无状态设计支撑）记录在证据 §4。见 `evidence/p2/service-slo-ceiling-20261023.txt` |
| 观测链路可靠性对账 | 已实现并在 SP3 实测 | 观测者自带健康账（每阶段重试、`observer.errors`、`observer.heartbeat`、退出时 `observer.summary.json` 报 `coverage_pct/longest_gap_ms/probe_failures/ingest_failures`）；执行服务侧新增消费端计数 `observations_accepted/observations_rejected/last_rejection`。**两侧数字必须相等才算没丢观测**：run5 实测 `ingested=6 = accepted=6`、`rejected=0`、覆盖率 91.73%、最长间隔 16602ms（即正常节奏）。同时把两个语义分清：从未有过观测 ⇒ 服务门 `not evaluated` 不拦；有过观测后断供 ⇒ `SERVICE_OBSERVATION_STALE` 拦；前者不能当后者用。另一项补测完成：绝对上限**解除后重新放行**有真机取样（合规窗 p99 0.1388ms、age 2529ms ⇒ `code=none hold=none`）。但同一运行的最终快照也暴露：上限一解除、抑制参数又压在下限时通道立刻来回切 2 次，所以“上限挡住切换”只能声明为被超限观测覆盖的那段时间内没有动作。见 `evidence/p2/service-slo-ceiling-20261023.txt` §7 |
| class 路由的配对消融（Cc−C / Dc−D） | 完成，**零结果**（2026-10-23，20 block × 5 臂） | 同 expert、同参数、同数据集、同 cgroup 放置，唯一差别是服务 cgroup 声明 `class=LAT`、BG 目标显式声明 `BG`。完整性：100/100 块 valid、80/80 带策略块 `classes.proof ok=yes`、100/100 `cgroup.proof` 读回放置、dmesg 0 条新 stall、原始件重建 yes、D/Dc 每块各 1 次切换。结果：**Cc vs C = −0.01% [−0.29,+0.24]（GET）/ +0.05%（SET），胜负 10/10 —— 彻底的零**；**Dc vs D = −0.18% [−0.45,−0.02]（GET）**，p99 4.87→5.35ms、p99.9 8.8→12.1ms，方向在 1/2/5ms 三档一致（略负）。相对 A0 的收益（Cc +1.48%、Dc +1.49%）与无 class 的 C/D（+1.61%/+1.64%）没有差别 ⇒ **class 声明不是性能开关**，既有收益仍来自 expert 的切片/预算参数作用在 NORMAL→COMP 路径。上一轮旗舰的 C +1.62% 与本轮 C +1.61% 几乎重合，属跨运行一致性观察、不作为配对证据。机制只有假设（1ms LAT 片段 + 抢占优先可能在这个排队主导的负载上增加调度事件），未写成结论。**边界**：只声明了 LAT（无 CACHE 工作集）、`llc_affinity=True` 与 `SP_DSQ_SHARED` 未测、块内改档未测（该轮构建不含 NORMAL 修复；r23 构建已修并有三阶段对账的单测覆盖）、单机单负载 n=20。见 `evidence/p2/flagship-classrouting-20261023.txt` |
| 旗舰正式矩阵（协议 §6.2） | 完成首轮（20 block × 5 臂，v2 第一批性能数据） | noisy-cpu redis 60k rps 开环、SLO 5ms、GET/SET 各 60s、块内配对、seed 20261024、10000 次配对 block bootstrap。**完整性**：100/100 块 valid、C/D 40/40 块结束仍 `enabled`、A2 每块读回 `cpu.weight=200`、原始件重算 goodput 与探针自报最大偏差 **0.019%**、超时 3 条全部计入、无块被剔除、收尾 `pins=0`。**结果**：C vs A0 goodput **+1.62% [+1.53,+1.82]（20 胜 0 负）**、SET **+1.28% [+0.83,+1.83]**（19/1），p99 **−47.9%/−32.5%**、p99.9 16.4→8.6ms；D vs A0 **+1.51%/+0.96%**；**C vs A2 +1.90%（20/0）** ⇒ 收益不是静态 CPUWeight 倾斜；**A1 vs A0 −0.82% [−1.07,−0.36]** ⇒ 纯观测栈自身是净成本且 p99 +14.4%；**D vs C −0.27% [−0.48,−0.05]（SET）且 p99 差 7.6–10.5%**，每块恰好 1 次切换 ⇒ 在这条负载上自动切换没赚回自己的空窗。BG 相对进度 0.997–1.001、等待 C 3.78ms < A0 5.98ms ⇒ 前台收益没有饿死后台。**阈值敏感性必须一起报**：@2ms 时 C SET −0.17%、D GET −0.00%、D SET −0.57% ⇒ 收益集中在 5ms 以上远尾，2ms 附近分布交叉。**范围**：单机单负载单 SLO 设定、跨机/未见负载未做；BG 是"未受损"不是"被优化"；四分类路由未接入控制面，本轮收益全部来自 NORMAL→COMP 路径的切片/预算。见 `evidence/p2/flagship-matrix-20261023.txt` 与 `evidence/p2/flagship-matrix-20261023/`（5.8GB 逐请求原始件留在 SP3，块内哈希已入库） |
| 旗舰矩阵重跑（冻结构建 `370180b`） | **完成：复现成立**（2026-10-25，同冻结参数 20 block × 5 臂，100/100 valid，原始件重算 ≤0.5%） | C vs A0 **GET +1.83% [1.38,2.26]（20/0）/ SET +1.39%（18/2）**、p99 −39.6%/−32.8%；D vs A0 +1.74%/+1.05%；A1 观测栈 GET CI 本轮跨 0（−0.55% [−0.94,+0.14]，SET 仍 −0.68% 不含 0）；A2 ≈0；**D vs C 新构建统计打平**（GET −0.19% [−0.33,+0.14]、SET −0.39% [−0.67,+0.18]，p99 仍 +5-6%——比旧轮"没赚回空窗"更弱的说法,如实并列）；@2ms 仍略负、本轮 @1ms C GET +2.5%；BG ×1.000-1.002。两轮跨日方差（A1 GET、D vs C 的 CI 变化）必须同报。见 `evidence/p2/flagship-rerun-20261025.txt` |
| B 臂补充矩阵（数据面基础成本） | **完成**（2026-10-25,冻结构建,10 block × A0/B,20/20 valid） | **B vs A0 GET +1.75% [1.45,2.09]（10/0）、p99 −40.8%**、SET +1.39%。收益链条闭合：A1(纯观测)≈−0.5% → **B(BASIC+观测)=+1.75%** → C(CLASS+观测)=+1.83%——旗舰量级的收益在 BASIC 形态就已出现,class 路径额外贡献≈0（与 class-routing 零结果互证）;B vs C 是跨运行对比,只能称打平。见 `evidence/p2/barms-20261025.txt` |
| E 臂补充矩阵（参考 schedpilotd 对照,R6） | **完成**（2026-10-25,10 block × A0/E,20/20 valid;先 1 block 冒烟） | **E vs A0 GET +1.92% [0.94,2.17]（10/0）、p99 −49.4%**、SET +0.03% 打平（5/5）、BG ×1.006。新旧控制面同负载收益同量级,一致性判据满足。探索性:E 的 @1ms +7.0% 高于 v2 C 的 +2.5%,n=10 不足以定论。调试事实入档:参考 daemon 只写 pinned map,挂载由参考 loader 负责。见 `evidence/p2/earms-20261025.txt` |
| mixed-foreground D-no-class 对照 | **完成**（2026-10-25,3 轮 × A0/Dnc） | **Dnc joint 2/3 = D 的 2/3**（svc-B 唯一 miss 同为 2ms 擦线）⇒ 联合 SLO 改善不需要 class 分离;与 B 臂、class-routing 消融三线互证"class 是契约不是性能开关"。见 `evidence/p2/mixed-fg-noclass-20261025.txt` |
| watch 重建身份拒绝 | **完成**（2026-10-25,`4d4f18e`） | arm 捕获 watch 的 kernfs inode,观测要求 path+身份双匹配;同路径重建 → 在位效应回退 + `WATCH_REBUILT` 冻结、不转授。unit 572 断言 + 真机 Phase F |
| X 臂 | **UNSUPPORTED（有据）** | 上游 scx 主线调度器已全部 Rust 化（scheds/ 仅 rust/experimental）,验收机无 Rust 工具链;现代上游目标内核高于本机 6.6。判定材料：codeload 拉取的 main 树（13.8MB tgz）。记入验收报告 §R7 |
| 演示与彩排 | **完成** | `scripts/demo_sp3.sh` 八步时间线,最终形态**连续三次 PASS**（rehearsal-3/4/5）,每次收尾 state=disabled、pins=0。调试事实两条入档（残留 state 使演示变恢复、apply 拒绝时 CLI 非零退出要先捕获再断言）。见 `evidence/p5/demo-rehearsals-20261025.txt` |
| gen-2 回归与包校验 | **完成** | watch 身份改动后 `ep-executord=35e80b99…`（gen-2,其余产物不变;改动仅在 --event-arm 路径,矩阵不经过）:验收 **r29 六轮 rc=0** + fail-safe **run10 17/17** + ctest 8/8;`scripts/verify_package_sp3.sh` 哈希/证据/契约三查**全过**。两代构建的声明见 `evidence/p1/frozen-build-20261025.txt` §4 与 `docs/ACCEPTANCE_20261025.md` |
| 泵/BPF 开销量化 | **完成**（2026-10-25） | 40k rps 负载下泵 **0.5% 单核**（60s/0.31s）、BPF 12.1k 事件/s 零丢弃零复用、goodput 配对差中位 **−0.010%** [−0.049,+0.592]（n=6）;p99 表观波动落在已知噪声带,不归因泵。见 `evidence/p4/pump-overhead-20261025.txt` |
| 实验矩阵 pilot（协议 §6.1） | 工具跑通，未达旗舰口径 | 3 块 × A0/C/D，种子 20261023 记录真实臂序，redis 7.2.15 固定 `-c 50 -P 16 -n 200000 -r 100000 -d 64`、GET/SET 分开报、预热 10s、washout 15s，BG 导出真实完成量。抓到的问题即本次产出：单次突发只有 **0.37–0.66s**（协议要 60s 窗）；D 臂块内发生 1 次自动切换（投影空窗 665ms）与测量窗同量级，短窗对比被切换事件支配；goodput@SLO 仍算不出（redis-benchmark 无逐请求记录，协议禁止由分位数推算）；**客户端是否饱和未证明**；缺 A1/A2 使 C/D 与 A0 的差混入观测与控制面成本；manifest 的 sched_ext 状态记在块开始（恒 disabled），证明挂载要看测量后的 `schedext.state/ops`；BG 单位粒度粗（约 0.5–1s/单位）且与在线观测的每秒工作量不同口径，不得相加。前两版 harness 每块泄漏 3 个实例 pin、分析脚本变量覆盖，均已修，run3 收尾 9/9 块齐、`pins=0`。**不进正式表、不产出性能结论**。见 `evidence/p2/pilot-matrix-20261023.txt` |
| 逐请求开环负载与矩阵 harness | 已实现并 5 臂 smoke 通过 | `experiments/redis_openloop_probe.c`：按固定到达率排程（服务变慢时 offered 不下降）、每条完成写 8 字节 ns 原始件、goodput@SLO 由逐请求判定直接计数（协议禁止由分位数推算）。自带标定 `redis_rate_calibration_sp3.sh`：5k–80k × GET/SET 全部 achieved=100%、0 超时 0 停顿、发生器 CPU ≤0.64 核；但 80k 时服务端已排队（p99 12.4ms、9.5% 超 5ms SLO）⇒ 旗舰 offered 率必须由 pilot 在 20k/40k/60k 之间冻结。矩阵 harness `redis_matrix_sp3.sh` 补齐 A1（同等观测 + `--skill mock` shadow，不 attach）与 A2（静态 CPUWeight，systemd scope 并把 `cpu.weight=200` 读回证明），BG 工作单位改成在线门控读的同一生产者，逐臂记录挂载证明（C/D `enabled`+`ops=eulerpilot`，A0/A1/A2 `disabled`），flock 互斥防互相污染；1 block/臂、每命令约 80 万条记录，**n=1、无 CI、不进正式表**。过程中被原始分布揭穿的 4 个客户端缺陷（EPOLLOUT 空转、延迟一轮 flush 造成 5ms 悬崖、`$-1` nil 失同步、补丁吃掉转义）与 5 个 harness 缺陷（幂等键短于 8 字符导致 C/D 在未挂载状态下交数据、metadata.name 大写被 schema 拒、`policy_json` 参数错位把 source 写成 "5"、根 cgroup 未委派 cpu、双跑互删目录）全部记录在案。见 `evidence/p2/matrix-harness-20261023.txt` |
| 观测断供的消费者侧可见性 | 完成（SP3 两阶段真机对照） | 控制面在每次门控评估时记 `observation_max_age_ms`（断供高水位）与 `stale_checks`，随 `policy.status.service` 导出，使断供**在恢复之后仍然可报告**；补报阈值与动作阈值分离：`10×window` 内允许占据"最新观测"，更旧一律拒收（`last_rejection="observation is too old to describe the present"`）且不覆盖现值。只读看门狗 `experiments/service_watchdog_sp3.sh`（9 类告警、逐 poll CSV、违例即 rc=1）与场景驱动 `experiments/service_observer_gap_sp3.sh`：健康 24s/12 poll **0 违例**（不误报）→ kill 生产端 25s → `age_ms=26032`、`stale_checks=8`、hold=`slo denied: SERVICE_OBSERVATION_STALE`（dwell 钉在 3600000ms，证据层仍排在抑制层之前，全程 0 次切换）→ 迟 20s 补报被收但门仍拒动、迟 120s 被拒且未覆盖 → 重启后 `age_ms=1044` 而高水位 26037ms/计数 12 原样保留，看门狗 35 告警五类命中 rc=1。顺带量到生产端节奏约束（周期 `window+interval` 必须严格小于 `2×window`）与 realtime 时钟无关，已写入协议 §5.1 第 6/7 条。两个计数是进程内遥测，不入 WAL、重启清零。不测性能。见 `evidence/p2/observer-gap-20261023.txt`；unit 353 条（+10）、`ctest` 6/6、r20 六轮 rc=0 |
| 持续服务负载下的调度器存活（runnable task stall） | 缺陷已定位并修复（真机验证） | 旗舰 pilot 的 6 个挂载块全部在约 46s 被内核摘除，受害者一律是 per-CPU kworker；配料分离（不加载/仅后台/仅服务/全部）证明触发条件是服务负载本身，`stats` 的 `enq_comp == dsp_comp` 否掉"DSQ 没人排空"，指向内核在 `move_task_between_dsqs()` 里对绑定任务的**亲和性改投**（→ 全局 DSQ → 只有它自己那个 idle CPU 能取回，而唤醒 idle CPU 按 `sched-ext.rst:228` 是调度器的责任）。第一版只补 idle 唤醒**未修好**（仍 45s 摘除，路径 90s 仅走 6–9 次，作为正确改动保留）；真正修复是 `nr_cpus_allowed == 1` 直投自己 CPU 的 local DSQ + `SCX_KICK_IDLE`，计数 `enq_pinned_local`。验证：同一失败配料 3×90s ⇒ 269/269 poll enabled、新增 stall 0 条、`degraded=False`、generation 未变。控制面无需改动：3s 内检测、补偿回退、pin 清理、原因进 `last_fallback`。**边界：不声称 stall 类缺陷已穷尽**，且该轮所有任务落默认类，LAT/CACHE/BG 三条类路由未经此压测。BPF 重建流程脚本化 `scripts/build_bpf_sp3.sh`。见 `evidence/p1/stall-runnable-20261023.txt` |
| 旗舰参数冻结（协议 §6.1/§5） | 完成（依据 pilot，pilot 数据不进正式表） | `experiments/flagship_params.json`：offered **60000 rps**、SLO **5ms**（同时报 1/2/5/10ms）、BG 单位 **probe5s** + 下限 **140 单位/秒** + 最长等待 **50ms**（schema 下限）、前向 p99 绝对上限 **10ms**、20 block、臂 A0/A1/A2/C/D、`seed=20261024`、配对 block bootstrap 10000 次。选择依据：40k 在 5ms 已贴顶（A0 good% 99.88–99.94，臂间差只剩 +0.02…0.12%），60k 处于"服务端排队、客户端未落后"的膝点（A0 96.86–98.12%，deficit≈1800rps，发生器 CPU 0.63 核）；BG 实测带宽 164.1–165.3 单位/秒（26/26 窗全齐）、等待 A0 5.2–5.8ms / C 2.6–3.0 / D 1.4–3.9。诚实边界：单种子单机单负载、阈值敏感性未消除（四档同报）、BG 门槛是"实测带宽+余量"而非业务承诺、p99 上限在 A0 偶发越线意味着"D 少切"与"门被自己的测量卡住"要靠 `last_hold`/`switches_recorded` 分开。见 `evidence/p2/flagship-params-20261023.txt` 与 `evidence/p2/flagship-pilot-20261023/`（含 `pre-fix-contaminated/` 作废数据备查） |
| mixed-foreground 双前台竞争 | 已覆盖（3 轮 × 2 臂） | 新驱动 `experiments/mixed_foreground_sp3.sh`：svc-A 30k rps/5ms/`class=LAT`、svc-B 15k rps/2ms/`class=COMPUTE`，外加 8 spinner 制造稀缺（空载时两者都轻松达标，测不到竞争）。`met` = 该服务 ≥99% 请求在预算内。**A0（裸服务、内核默认）**：svc-A 29928/p99 4.44（3/3 达标），**svc-B 11495（offered 的 77%）/p99 4.02（0/3）**，joint **0/3** ⇒ 预算更紧的那个先崩，而只看 5ms 服务会误判没问题。**D（执行服务 + autopilot）**：svc-A 30000/p99 1.30（3/3）、**svc-B 14860/p99 1.98（2/3）**，joint **2/3**——唯一 miss 是 p99 2.05ms 擦线。**不能说收益来自 class 分离**：D 与 A0 的差别同时含调度器、expert 参数与观测栈，缺 `D-no-class` 对照。同轮修了产品缺陷：策略词汇 `COMPUTE` 与 helper 的 `COMP` 不一致，首次声明 COMPUTE 时 class-set 失败、apply 被拒；映射放在 `ctrl_class_set` 边界，并在 `integration_fd.sh` 补了 COMPUTE 目标与断言（这条测试本该早发现）。见 `evidence/p2/mixed-foreground-20261023.txt` |
| phase-change / noisy-mixed 时间线 | 已覆盖（2 条时间线 × 4 相 × 60s） | 新驱动 `experiments/phase_change_sp3.sh`：quiet→noisy-cpu（8 spinner）→noisy-mixed（再叠 3×96MB 内存带宽）→quiet，40k rps，逐相独立 probe 与原始件，每 2s 轮询并把切换归因到相。**A0（裸服务、内核默认调度）**：mix 相 goodput **38.96k/39.08k**（掉 0.6–1.0%）、cpu/mix p99 **4–5ms**；累计超预算 **0.71%**。**D（执行服务 + autopilot，开局交租约）**：四相 goodput **≥39.97k**、p99 **≤1.2ms**；累计超预算 **0.02%**；每条时间线只切换 1 次（r1 在 quiet、r2 在 cpu 相），noisy 段一直是 `already in place: latency-balanced` ⇒ 抑制层没有在相变处抖动。恢复：goodput 无残留（quiet-again 39918–39999）；**p99 恢复未测**（方向不一致，且本轮没有测进入稳态时间）。**边界**：对照组是"系统 D（含常驻观测者）对裸 A0"，观测者只在 D 侧运行 ⇒ 结论偏保守，但 **PSI 不可跨臂比较**（D 的 quiet 相 `some avg10` 23.8–24.4 主要来自观测者自身）；缺少 C（固定 expert、无观测者）臂。mixed-foreground **未做**；cross-machine **UNSUPPORTED**（唯一验收级主机 SP3，SP4 因 cgroup v1/无 PSI 被排除）。见 `evidence/p2/phase-change-20261023.txt` |
| 切换延迟的代价（重设计） | 已测：可分辨区间内为 **0** | 拆成 `代价 = 单位时间机会差 × 40s 延迟`，用**固定 expert、不切换**的静态对照量第一个因子（`experiments/expert_gap_sp3.sh`，每轮交替顺序，并断言 `active_expert` 等于声称值）。**过载区间**（60k + 16 spinner，n=6）：差距看似很大但 **CI 全部跨 0**（goodput +37.5% [−41.9,+134.4]、p99 −45.2% [−60.6,+60.9]，逐轮符号翻转；同臂 p50 在 2.3–14.4ms 间跳、在途 1159–3001）⇒ 机器双稳，**该区间不可测**，不是"专家没差别"。**稳定区间**（40k + 8 spinner，n=6）：两臂 goodput 皆 39997–39999（100% offered）、p99 1.11–1.29ms，差值 **goodput −0.00% [−0.01, 0.00]** ⇒ 40s 延迟的代价约 **0–30 个请求**（只能当上界）。结论：**没有证据表明门控延迟造成吞吐损失**，在能测的区间它确定为零；"过载下该不该切"仍需控制队列状态的设计才能变成可测问题。同轮修了测量件 bug：探针把起止 wall clock 打印成同一值（`86b62652…`→`5f0213fd…`），此前 goodput 数字不受影响（探针内部用单调时钟且逐请求重建一致到 0.019%），但 `*_at_ms` 字段是假的。见 `evidence/p2/expert-gap-20261023.txt` |
| 门控在"应当触发"场景下的行为 | 行为已量化，价值未测出（4 轮） | 驱动 `experiments/gate_injection_sp3.sh`：基线 20s（不交租约）→ 16 spinner 注入 + 静置 10s → 交租约测 45s → 撤载。`D`（上限 1ms）每轮 25 次 `slo denied: FOREGROUND_P99_EXCEEDED`，切换推迟到第 26 次轮询（≈52s，注入已撤）；`Dng`（阈值不可 bind）第 6 次轮询（≈12s）即切换 ⇒ 门把动作推迟约 40s，且证明它是**延迟器不是否决器**。价值未测出：注入期 goodput 臂内波动 85–93%（D 19.8k–51.1k、Dng 19.2k–53.5k），逐轮配对 −4.6%/−17.4%/+170.8%/−23.7%，**不得引用其中位数**。同轮测量事实：静息 p99 1.7ms 主要是 idle 退出延迟（96 个 1ms sleeper 使其降到 0.23ms 且 goodput 满血）；1ms 周期探针 p99 对 CPU 抢占不敏感（16 spinner 仅 2.5–4.3ms，而 goodput 掉到 1/3）；BG 进度在争抢下反而升高（163→176，压到 1k goodput 才 112）⇒ `BG_PROGRESS_FLOOR` 对这类伤害不响；2ms 预算下门抖动（11–13 次拒绝后在 1.71ms 窗口放行）。见 `evidence/p2/gate-injection-20261023.txt` |
| 消融（探索性，10 block） | 完成，**两个 null，但含义不同** | `Dng`（服务门控"不可能触发"：floor 0.001、wait 10000、ceil 60000；schema 不允许 floor=0，故只能表达为永不 bind）vs `D` = **+0.21% [−0.22,+0.60] / −0.24% [−0.78,+0.99]**；`Dnv`（`bg_vtime_pct` 200→100）vs `D` = **+0.25% [−0.13,+0.65] / −0.30% [−1.06,+0.22]**。**Dng 的 null 是结构性无信息**：30 个自适应块的 `last_hold` 全是 `already in place`、**0 次 `slo denied`**、0 个 adverse 档 ⇒ 本轮的门从没拦过东西，只能说明"不触发的门没有可测开销"；要衡量门的价值必须构造它应当触发的场景（饥饿注入/越限），已列为下一步。Dnv 的 null 有信息量：取消 BG vtime 惩罚后前台无可测变化、**BG 进度也没变**（163.5 vs 163.5 单位/秒）⇒ 在这条负载的 BG 强度下护栏近乎免费、也近乎不起作用。未做且原因各异：no-preempt（无开关）、no-pmu（无集成，UNSUPPORTED）、no-llc（false 即默认，true 路径未验证）、no-router（即 C 臂）。**证据缺陷**：本轮每块 `manifest.json` 的 `ablation` 字段记录成了同块上一个臂的值（决策块原先写在 manifest 之后），harness 已修正；本轮归属以逐块 `policy-adaptive.json` 读回值为准，manifest 该字段不得引用。见 `evidence/p2/flagship-ablations-20261023.txt` |
| 类路由覆盖面 | 已接入并压测（2026-10-23）；LLC 亲和与共享队列仍缺 | `spec.targets[].class`（`NORMAL\|LAT\|COMPUTE\|CACHE\|BG`，可选）+ 生效类唯一出口 `effective_runtime_class(role, declared)`：未声明即保持旧行为（BACKGROUND→BG，其它不写 class_map），故上一轮正式矩阵的语义逐字不变。不变量 `class=BG` 必须 `role=BACKGROUND`（反向亦然）把"禁止把陌生任务降为 BG"变成结构约束。运行时标记 `bg_pids/bg_cgroups`→`classified:[{pid|path,class}]`，`managed_class_` 记 pid→类；刷新区分"类没变→保留 / 类变了→就地重设（否则一个周期内退回 NORMAL）/ 撤下→class-del"。NORMAL 不写条目（防 PID 复用继承策略从未声明的分类）。unit（`test_policy_class_declaration`：真值表 + 四条拒绝；unit 总计 375）+ 真内核集成（`integration_fd.sh` 声明 LAT → `class-get` 从内核 map 读回 LAT，BG 撤销路径不变）+ 存活压测（`experiments/class_route_soak_sp3.sh`：LAT=redis、CACHE=2×64MB 内存密集、BG=与门控同单位的生产者，3×90s ⇒ `POLLS 539/539 enabled`、`enq_lat 282/dsp_lat 282/run_lat 1.39M`、`enq_cache 762/dsp_cache 762`、dmesg 0 条新 stall、`degraded=False`；第一次 `enq_cache=0` 是驱动脚本用了 `$$` 而非 `$BASHPID`，记在证据里因为"没流量"和"没接上"在计数器上同形）。**仍缺**：`llc_affinity=True` 的 CACHE 分档路由、`SP_DSQ_SHARED`（控制面无开关）、以及**带 class 声明的正式矩阵**——上一轮是"NORMAL→COMP + BG"形态的结果，不能追认为四分类路由的收益。见 `evidence/p1/class-route-20261023.txt` |
| cgroup Skill（R5 动作域） | 已实现并在 SP3 真机验证 | 新增第二个效果域 `--skill cgroup`：写 cgroup v2 `cpu.weight`，写前快照、**写后读回**（`O_TRUNC` 是实测出来的必需项——短值覆盖长值会留残尾，`200` 写 `50` 读回 `500`）、每刷新周期漂移再断言、补偿回退、**崩溃后由 marker 恢复**（cgroup 文件不会随进程消失，所以这条链路必须有恢复）。新契约 `CgroupPolicy`（`design/schemas/cgroup-policy.schema.json`）：`effects[1..8]{path,cpu_weight 1..10000}`、`source=human`、`safety.policy_ttl_ms` 复用既有 TTL 回退。域与 skill 不匹配直接 `UNSUPPORTED` 拒绝且零副作用。层级：unit（35 条新断言，含生效值真值表与 11 项拒绝）+ **真内核集成** `tests/integration_cgroup.sh`（systemd scope、apply→越权改→再断言→rollback→SIGKILL→重启恢复→跨域拒绝），已注册为第 7 个 CTest 必测项。**边界**：只有 cpu.weight 一个旋钮（`cpu.max`/io/memory 未做）；`source` 暂只允许 human——自主来源要先拿到与调度域同等的租约/权限约束 |
| eBPF 事件触发链路（R5 感知端） | **已实现并在 SP3 真机闭环**（2026-10-25） | `sched/events_trace.bpf.c`（raw_tp sched_wakeup/wakeup_new/switch/process_exit；wait=start_time 校验后的 wakeup→首跑差值,按 cgroup 聚合）→ 只读泵 `ep-evtmon`（每窗口排空聚合 map、kernfs id→路径、event.observe 上报）→ 执行服务 `event.observe` 摄取 + `--event-arm` 人工授权 + 监控线程武装守卫（租约权限门→证据门→持续/回落抑制→经 handle_apply/handle_rollback 动作）。CgroupPolicy source 新增 `event-template`,仅被事件证据门放行（arm 在场+arm 属主持租约+观测新鲜）,杀泵即冻结。真机五相时间线（integration_events.sh,第 8 个 CTest 必测项）：空载不动→饥饿升权 100→5000→撤载补偿回 100→杀泵后 STALE 冻结+TTL 兜底→泵单独运行零写入。ctest 8/8、unit 550。**边界**：无性能结论;watch 按 path 匹配、cgroup 重建身份未拒绝;泵开销未量化。见 `evidence/p4/events-link-20261025.txt`。**R5 两半至此齐了**（动作端见 cgroup-domain-20261024） |
| 协议 §8 fail-safe 矩阵（故障恢复层，真机） | run4 为 15/16（S14 FAIL），**最终 run8 为 17/17** | 新驱动 `experiments/fail_safe_matrix_sp3.sh`：16 条目一次跑完——class/basic 两模式的挂载→故障→回退（S01/S02）、adaptive 在真实 RPC 被拒且零副作用（S03）、挂载中 SIGKILL 执行服务（S04）、sched-ref 下 SIGKILL loader（S05）、租约到期=心跳停止的等价路径（S06）、策略 TTL 到期（S07，TTL 必须≥schema 下限 10000ms）、无效策略（S08 未知字段 + `class=SUPER`）、越界与错误类型（S09 `max_bg_wait_ms=20000`、整数字段写浮点）、generation 冲突（S10 裸请求 +7）、第二实例（S11）、外部改值（S12）、成员迁出（S13）、日志盘满（S14）、CPU 热插拔（S15）、不可构造条目如实标注（S16）。每条都判"本场景是否新增/遗留自己的 pin"并核对 `state`；run4 收尾 `state=disabled`、pin 根为空。**S14 是真缺陷**：`ep rollback` 前置的 `lease.acquire` 要写 `state.json`，ENOSPC 下直接 `REJECTED` 且错误码被写成 `SCHEMA_INVALID`，回退请求 `attempted=false`，最后一条策略仍挂在内核里；同一现场的 TTL 通道与 SIGTERM 收尾都能撤除效应，但"要靠杀进程才退场"不是 fail-safe。**边界**：不证明 stall 类缺陷已穷尽；跨域第二步失败仍只有代码路径。见 `evidence/p1/fail-safe-20261024.txt` |
| 执行服务退出必须自己收尾 | 已实现并真机验证（本轮缺陷修复） | 原先收到停止信号只关 socket、删 pid 文件、返回：调度器靠进程退出顺带关闭 link fd 被内核摘掉，但没人 unlink pin、也没人走回退事务——run2 里 S12/S15 各留一个含 5 个 map pin（cfg/class_map/cpu_llc/stats/tg_stats）的实例目录。新增 `Executor::shutdown()`：停监控线程后若仍有生效效应，走同一条补偿回退（含 `verify_rolled_back`）再收尾。unit `test_executor_shutdown_releases_active_effect`（效应回退、generation 推进、二次 shutdown 不重复写记录）。协议 §8 "退出服务能否 detach 取决于 link 实际持有与 pin，验收读内核状态而非进程退出"由此才有真机证据 |
| 回退后的残余效应读回（§8"不能只看 cfg.flags"） | 已实现并真机验证 | `revoke_managed(error, confirm_readback)` + `ctrl_class_get` + `residual_effects`：撤销必须由 map 自证——`class-del` 后 `class-get` 报"不存在"才算撤销；孤儿回收从 `sched-runtime.json` 的 `classified` 重建受管集合（pid 直取，cgroup 按当前成员展开）趁 map 可读时逐条撤销并读回；`verify_rolled_back` 的通过语改为 "…no class or cfg outlives the unpin"，pin 残留时把还读得到的片长/vtime 与还活着的 class 条目一起报出。刷新路径**不开**确认：频繁换成员的 cgroup 会在毫秒级预算里每次多付一次 helper。真机证据：S01（LAT/BG 读回 + 撤销确认 + pin 目录消失）、S13（迁出后读回为不存在、再入组重新分类）。unit `test_rollback_proves_class_removal` 覆盖正反分支（删除空转→报 `class revocation unconfirmed` 且仍完成 detach） |
| 外部改值：cfg 会纠正、class 条目不会 | 部分闭合，限制如实登记 | refresh 开头加一次 cfg 读回（实测 **5ms/次**），与生效策略 `cfg_after` 不一致就写回并打印 `[sched-skill] cfg drift corrected: cfg field 'lat_slice_ns' …`；S12 用 `ep-schedctl set --pin-dir <inst> --json '{"lat_slice_ns":4000000}'` 扮演越权写者（111111 会被 helper 自身下限 250000 拒掉，那测的是参数校验不是漂移检测），在 12s 等待窗口内被纠正（约 6 个刷新周期；驱动只记"被纠正"，未记第几周期）。**class 条目同样已闭合（2026-10-25）**：refresh 第 4 相每周期轮换抽样一条受管条目读回比较（策略 COMPUTE=内核 COMP 的换算在比较处）、漂移即重设并打日志；真机 `experiments/class_drift_sp3.sh` 两场景（越权改 COMP、越权删除）均被纠正且恰各留一条日志,回滚无残余（`evidence/p1/class-drift-20261025.txt`）。边界:纠正时延=轮换周期,不是实时防护;只覆盖本实例管理过的条目。**披露**：cfg 纠正与抽样两改动均在旗舰矩阵构建之后,C/D/A1 的数字未在新构建上重跑 |

| 状态目录写不下去时的拆除（S14 缺陷修复） | 已实现并真机重验 | `lease.acquire` 的前置写 `state.json` 在 ENOSPC 下曾让整个 `ep rollback` 退不成（`REJECTED` + 误报 `SCHEMA_INVALID`、`attempted=false`、策略仍挂内核）。现改为**方向不对称**：存储写失败时授予"仅内存租约"（回 `lease_persisted:false` + `warning`）并把服务标 degraded——足以拆除已有效应（回 default fair 永远是安全方向），但 `policy.apply` 被 `RECOVERY_REQUIRED` 拒绝，直到 reconcile 成功；`policy.status` 新增 `store_warning`（恢复之后仍可报告）；回退自身遇写失败如实回 `DEGRADED` + `rollback.status ∈ {AUDIT_PENDING, EFFECT_RESTORED}`。契约扩展见 `docs/p1/ABI_FREEZE.md` §6 第 8 条；unit `test_lease_survives_unwritable_state_store`（把 `state.json` 换成同名目录制造 rename 失败，无需产品侧注入口）。真机 S14：`second apply under ENOSPC -> RECOVERY_REQUIRED`、`store_warning='lease is in memory only: write failed: No space left on device'`、`rollback under ENOSPC -> status=DEGRADED`，之后 `state=disabled`、pin 清空、目标继续推进 |
| 两步写入的第二步失败（S17） | 已实现真机构造（原缺证据项） | 把 `ep-schedctl` 换成只拒绝 `class-set` 的包装器并记录每次调用，apply 走 `get → set → class-set(被拒)`。真机读数：`status=ROLLED_BACK`、`error.code=APPLY_FAILED` 且 message 点名被拒的那一步、`generation` 仍为 0（未提交）、`state=disabled`、pin 根为空、目标推进；换回真实 helper 重试可正常 `VERIFIED` 并干净回退。**更正一处预期**：失败发生在已经挂载之后，契约是"补偿成功 = `ROLLED_BACK` + `APPLY_FAILED`"，`REJECTED` 只属于还没碰内核就拒的情况（run7 因此失败）。严格说这是同域两次写（仍在一个 Skill 内），跨域 saga 属 P4 |
| fail-safe 矩阵最终态（run8） | run8 后于 2026-10-25 在冻结构建 `370180b` 上以 run9 复跑：**17/17 PASS、收尾干净**（`/root/epv2-evidence/fail-safe-20261025/`）；构建与验收 r28 六轮的冻结记录见 `evidence/p1/frozen-build-20261025.txt` | S01–S17 全通过，收尾 `state=disabled`、pin 根为空。P1 判定改为**达成（准出成立）**，附带的边界与非阻塞遗留见 `docs/p1/P1_EXIT_REPORT.md` §1/§5。回归：unit 443 条断言、`ctest` 6/6、验收 r26 六轮 rc=0（`evidence/acceptance-sp3-20261024-r26/`），与 run8 同一份二进制 |

P1 退出门槛判定见 [docs/p1/P1_EXIT_REPORT.md](p1/P1_EXIT_REPORT.md)：**判定为达成**，
但结论附带不可省略的边界——adaptive/心跳/PMU 三条在 v2 上无法满足（UNSUPPORTED，不是通过）、
stall 类缺陷未穷尽、崩溃后 pin 靠同实例重启回收（运维约束）、class 越权改已由抽样再断言闭合（2026-10-25）、
以及 §3 的四项改动都晚于旗舰矩阵构建（**C/D/A1 的数字必须在冻结构建上重跑才算 P3 结论**）。
P1 达成不等于 P2/P3 达成：门控的收益仍未测出，P4 的 cgroup Skill 与 eBPF 事件链未开始。
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
