# EulerPilot v2

面向 **2026 年全国大学生计算机系统能力大赛—操作系统设计赛—中西部与华南区域赛—功能挑战赛道**，围绕「面向 openEuler 的自适应资源管控 Agent」重新设计的独立项目。

## 参赛信息

- 高校：西安邮电大学
- 队伍：三驱兄弟
- 队伍 ID：T2026116649912743
- 赛题：proj2（功能挑战赛道）

**当前状态：P0 技术验证通过，P1 部分实现且未收口，P2 为 advisory 切片。控制面、v2 自建 loader（每实例 pin、`eulerpilot` ops）、cfg 参数写入、SchedulingPolicy 校验与 cgroup/pid 目标绑定（cgroup_id、单次apply持有 pidfd/starttime）、租约/策略 TTL 过期自动回退、apply deadline 强制已跑通 SP3 真实闭环；执行服务通过 FD 传递持有 link FD；FeatureWindow、advisory 规则路由、SLO 门控与非人工来源 SLO 否决已接入；A/B/C liveness 已通过；显式 BACKGROUND 的 pid/cgroup 目标已展开到 class_map 并由执行服务每 2s 刷新；helper 已加整轮刷新预算（204ms/3 pid）和同期限 EOF 收割（208ms）；WAL append 失败会标记 broken/回退末条，影响已建立时仍执行紧急 detach 并报 DEGRADED/AUDIT_PENDING；active 持有真实 pidfd/cgroup 目录 FD，刷新逐绑定重验并撤销迁出的 BG 成员；BPF 数据面已接入 cfg v3 预算、per-CPU 真实运行时长记账和 aging：单 CPU 封闭竞争下把预算表达为片长配额（`lat_slice = comp_slice × pct / (100 − pct)`）后，60% 预算实测 LAT 57.9–58.5%（3 轮）且 `throttle` 每轮触发、BG 仍占 10.1–10.5%、各类最大间隔 ≤55ms；此前“LAT 31% 且 throttle=0”的原因是份额实际由片长比决定、记账又用名义 slice 低估真实占用，两者的读数错误（per-CPU 统计未求和）也已定位修正，见 `evidence/p1/fairness-budget-20261022.txt`；这仍是逻辑正确性验证，不得作为性能结论。已修正 `enable_seq` 校验并补上生效后的持续在位性检查：内核 `scx_enable()` 先让 `state` 可读、打印 banner、发同步 uevent、解锁，**最后**才递增 `scx_enable_seq`（`kernel/sched/ext.c:5503–5518`，全文只有这一个递增点；CPU 热插拔走独立的 `scx_hotplug_seq`），而 apply 原本一看到 `enabled` 就读计数器，于是记到递增前的旧值、随后 `verify` 报 `changed`——修前 7 次验收失败 2 次就是这个发布窗口造成的，不是第三方干扰。现在 apply 用 snapshot 的 pre-enable 值为基准、等计数器发布并连续两次读数一致后再记录（`wait_enable_seq_stable`），`enable_seq` 恢复等值判定，并成为**同名实例顶替**唯一可用的信号（link FD 查不出在位状态：6.6 的 `bpf_struct_ops_link` 没有可查询的 attached 标志，`.release` 为空且无 `.detach`）。执行服务每 1000ms 用新增的 `Skill::liveness` 探测，它与 `verify` 共用同一个 `presence_ok`（link FD 或 loader pid+starttime、state、ops、enable_seq），连续 3 次失败约 3 秒才走既有自动回退，瞬态单次读取不误撤；假 sysfs 单测覆盖顶替检测与发布窗口两个场景。剩余边界：`fd_passing` 的在位性覆盖边界（2026-10-23 明确）：正分支（执行服务持有 link FD 时逐项校验）由 `tests/integration_fd.sh` 在**真内核集成层**覆盖——它用 `--skill sched` apply 并断言两次 `VERIFIED`，而 `verify` 走的正是同一个 `presence_ok`；负分支（`link_fd_ < 0` 报 `executor does not hold the struct_ops link fd`）此前记为 UNSUPPORTED-at-unit，**2026-10-23 更正为已覆盖**：`SchedSkill` 的构造函数本来就接受 `SchedPaths`，直接以 `fd_passing=true` 构造出的实例其自然状态就是"没有 link FD"，因此 `liveness()`/`verify()` 可以在 unit 层直达该分支——不需要任何产品侧注入口或后门（此前判断"没有合法注入点"是错的）；同一测试还覆盖了 `sched-ref` 的 `loader process is gone or was replaced`、`verify_rolled_back` 在未持 FD 时继续查内核状态、以及 applied 数据不可解析时的拒绝（`test_sched_skill_presence_branches` 12 条）；真实顶替已用第三方调度器实测：已有调度器生效时，**第二次 struct_ops 注册被内核以 EBUSY 拒绝**（参考实现 `scx_schedpilot.c:376`、v2 自己的第二个 loader `sched/loader.c:468`），运行中的调度器、`root/ops` 与 `enable_seq` 都不变——“存活期间被顶替”根本无法构造，因此顶替检测仍只有 unit 级证据，`enable_seq` 等值判定也从“同名顶替唯一信号”下调为快速 disable+re-enable 采样盲区的兜底（`evidence/p1/takeover-thirdparty-20261022.txt`）。另修掉一个可观测性缺陷：启动期 degraded 的原因原先被丢弃（日志 `orphan recovery failed:` 冒号后为空、apply 只回笼统提示），现由 `startup_degraded_reason` 贯通日志、`policy.status` 与 apply 拒绝 detail 并有单测，见 `evidence/p1/takeover-20261022.txt`。见 `evidence/p1/enable-seq-publish-20261022.txt` 与 `evidence/p1/liveness-watch-20261022.txt`（`enable-seq-verify-20261022.txt` 的根因判断已在该文件第 6 节更正）。自动 apply 已实现最小切片并在 SP3 实测：默认关闭，需 `--auto-apply --auto-policy`（baseline 校验不过就不启动），只能改 `spec.expert` 且在三个已验证档位内、targets 与 safety 沿用人工 baseline、`source` 记为 `rule-template` 故必过 SLO 门控，并且**只有把租约显式交给它（`ep lease --owner`）才动作**，别人取得租约即停；SP3 验证了交接前不动、PSI 越过阈值后随路由切换到 latency-balanced、租约被收回后 generation 不再推进，切换走 rollback→apply，代价已量化：每次留下 **303–665ms 无 v2 策略的空窗**，同负载下空窗内 1ms 周期唤醒的 p99 从 1.2–2.0ms 恶化到 13.2–16.1ms、max 达 20–23ms，而撤载后的空窗测不出毛刺；同一次 120s 实验里它来回切了两次，3s 冷却挡不住阶跃负载，因此自动 apply **不能默认开启**；已按这个代价实现切换抑制并按逐门关重放在 SP3 验证：判定分权限→证据→抑制三层（顺序本身是契约），抑制层为 `dwell`（默认 60s，当前效果在位不足不得离开）、`candidate_hold`（路由目标须稳定 15s）、冷却与**切换预算**（10min 内最多 2 次，失败的动作照样消耗，因为回滚已经真的付过一段空窗），`policy.status.auto_apply` 会报出预算占用与投影无保护时间。三轮隔离重放分别证明每个门都能单独成为决定门：dwell 挡到 `in place 59755ms`/60000 才切、候选保持挡到 38161ms/40000（同轮 dwell 行数 0）、预算 1 次用完后 100s 观察窗内 65 次拒绝回落。自动通道仍**默认关闭**、不在交付路径里，且抑制只是限速——没有服务级收益信号，无法判断"这次切换值不值 0.6s 空窗"。见 `evidence/p2/auto-apply-pilot-20261022.txt`、`evidence/p2/switch-downtime-20261022.txt`、`evidence/p2/autopilot-suppression-20261022.txt`（该文件第 6 节更正了上一版对断言计数与目录引用的一处错误）。加载场景的服务级 SLO 反馈已实现并在 SP3 闭环：观测契约 + `service.observe` RPC（`ep observe`）不取租约、不写调度器、不推进 generation，**只能让门更严**；`SloGate` 的服务半区只按生效策略 `spec.safety` 声明的阈值判（没声明就不发明），顺序是"该档此前被实测判过退化 → 观测过期 → 背景进度下限 → 等待上限"，且排在宿主特征之前，没有观测时报 `not evaluated` 而非通过；切换真正换了 expert 才武装切换后判定，并要连续 `--auto-outcome-windows`（默认 2）个窗口都违规才把该档写进 `adverse_experts`。SP3 实测确认：租约未交时基线窗口内不动、切换后判定被武装并解析、两窗才确认（单窗只记 suspected）、被禁档不连坐其它档与人工回滚、饥饿注入后 `BG_PROGRESS_FLOOR` 使自动通道停住。**本轮最重要的量化是负面结果**：同一状态下连测 8 窗，p99 极差/均值 46%（背景进度只有 5.4%），而策略声明的退化预算是 5% —— 所以进度类约束在这个测量精度下可用，**p99 类约束目前不足以驱动自动判定**；为此加了分辨率护栏——策略要求的敏感度若不大于该信号实测的相邻窗抖动，比较类判定**不执行**并明报 `comparative check not run`，既不判违规也不判通过（抖动只由同档非待判定窗口算出，相差 4 倍以上视为状态变化而非噪声）；在线口径已据此写入 `docs/EXPERIMENT_PROTOCOL.md` §5.1（窗口 15s、观测必须常驻、前向延迟暂只用绝对量），并已把其中两条落到真机：`spec.safety.max_foreground_p99_ms` 可选绝对上限（实测 p99 1.545ms > 声明 1ms ⇒ `FOREGROUND_P99_EXCEEDED`，同期宿主特征合格、观测未过期、自动通道全程 0 次切换；该检查不写入按档记忆，超限消失即放行）与常驻观测生产者 `experiments/service_observer_sp3.sh`（5 轮 15s 连续窗口从未触发 `STALE`），见 `evidence/p2/service-slo-ceiling-20261023.txt`；观测链路现在两侧对账（观测者报覆盖率与失败数、执行服务报 `observations_accepted/rejected`，run5 实测 6=6、覆盖率 91.73%，且“从未有观测”与“观测断供”是两种不同状态），绝对上限解除后重新放行有真机取样，但同一运行显示上限一解除、抑制参数又在下限时通道立刻抖 2 次；实验矩阵已完成 pilot（3 块 × A0/C/D，种子入档）：**工具跑通但口径不达标**（单次突发仅 0.4s 而协议要 60s 窗、块内切换与测量窗同量级、goodput@SLO 还算不出、客户端未标定、缺 A1/A2），故 pilot 数据不进正式表，v2 仍无性能结论；另有一次运行显示观测断供超过两个窗口后所有档都被拒，即"接服务门控必须接常驻测量"。断供的消费者侧可见性已补上：执行服务在每次门控评估时记 `observation_max_age_ms`（断供时长高水位）与 `stale_checks`，所以**断供过去之后控制面仍然报告它**（SP3 实测恢复后 `age_ms=1044` 而高水位 26037ms、计数 12 原样留着），早于 `10×window` 的补报才允许占据"最新观测"、更旧的一律拒收且不覆盖现值；只读看门狗 `experiments/service_watchdog_sp3.sh` 有 9 类告警并以违例计数决定退出码，真机两阶段对照为**健康 24s/12 poll 0 违例、生产端被 kill 25s 五类告警全命中 rc=1**，同期 hold 报的是 `slo denied: SERVICE_OBSERVATION_STALE` 而不是被钉在 3600000ms 的 dwell——证据层排在抑制层之前这件事第一次有了读数。顺带量到一条运维约束：生产端周期是 `window + interval` 而判据是 `age ≤ 2×window`，`interval = window` 会在健康机子上闪断，已写进 §5.1 第 6/7 条（见 `evidence/p2/observer-gap-20261023.txt`）。**旗舰参数 pilot 还暴露了一个 P1 级数据面缺陷**：挂载 eulerpilot 后跑 60k rps 的 redis，**6 个挂载块全部**在约 46s 时被内核以 `runnable task stall` 摘除调度器（受害者一律是 per-CPU kworker，报告 34.4–45.1s），同期执行服务 3s 内检测并按补偿路径回退（`degraded=true`、`last_fallback="scheduler lost…"`、pin 清理干净）——控制面这条链是对的，问题在数据面。配料分离（不加载 / 仅后台 / 仅服务 / 全部）定位到触发条件是**服务负载本身**；`stats` map 显示 `enq_comp == dsp_comp`（没有任务卡在我们的 DSQ 里等被取走），从而否掉"某类 DSQ 没人排空"，指向内核的**亲和性改投**路径：绑定任务被别的 CPU 从共享 DSQ 拉走时 `move_task_between_dsqs()` 判定该 CPU 不允许，于是改投全局 DSQ，而只有它自己那个（idle 且无人唤醒的）CPU 能把它取回来 → 30s 看门狗摘除。第一个假设（给非唤醒 enqueue 补 `scx_bpf_kick_cpu` 唤醒 idle CPU）**没有修好**（仍 45s 摘除，90s 内该路径只走 6–9 次），作为内核文档要求的正确改动保留；真正的修复是 `nr_cpus_allowed == 1` 的任务不进共享类 DSQ，直接投到自己 CPU 的 local DSQ 并 `SCX_KICK_IDLE`（新计数 `enq_pinned_local`）。验证：同一失败配料 3×90s，`269/269 poll enabled`、`DMESG_new_stall_lines=0`、`degraded=False` 且 generation 仍为 1，修复前同一机器 6/6 失败。BPF 对象与 skeleton 改由新增的 `scripts/build_bpf_sp3.sh` 重建（此前重建流程只写在 README 散文里，无法按需复现）。**边界：不能声称 stall 类缺陷已穷尽**——验证运行里 `enq_lat/enq_cache/enq_bg` 全为 0（策略只有一个 FOREGROUND 目标，所有任务落默认类），LAT/CACHE/BG 三条类路由还没被这样压过；旗舰矩阵会真实使用这些类，看门狗仍是兜底。见 `evidence/p1/stall-runnable-20261023.txt`。随后把 **LAT/CACHE 接进了控制面**：`spec.targets[].class`（`NORMAL|LAT|COMPUTE|CACHE|BG`，可选）与业务 `role` 正交，生效类只有一个出口 `effective_runtime_class(role, declared)`——未声明即旧行为（BACKGROUND→BG，其它角色不写 class_map），所以已完成矩阵的语义逐字不变；`class=BG` 与 `role=BACKGROUND` 双向强制，把"禁止把陌生任务降为 BG"从纪律变成结构约束。运行时标记改为 `classified:[{pid|path,class}]`，刷新按类重述：类变了就地重设而不是先删后设（否则任务会在一个刷新周期内退回 NORMAL），`NORMAL` 干脆不写条目（否则 PID 复用者会继承这条策略从未声明的分类）。真内核验证：`integration_fd.sh` 声明 LAT 后用 `class-get` 从内核 map 读回 `LAT`，事务回报同时带 `class` 与 `effective_class`；存活压测 `experiments/class_route_soak_sp3.sh` 让 LAT=redis、CACHE=2 个 64MB 工作集进程、BG=与门控同单位的生产者同时被喂，3×90s ⇒ **539/539 次轮询 enabled、`enq_lat 282/dsp_lat 282`、`enq_cache 762/dsp_cache 762`、dmesg 0 条新 stall、`degraded=False`**（第一次 `enq_cache=0` 是驱动脚本用了 `$$` 而非 `$BASHPID`，"没流量"和"没接上"在计数器上同形，故记档）。单元 375 条、`ctest` 6/6、验收 r22 六轮 rc=0。见 `evidence/p1/class-route-20261023.txt`。 接上之后做了同块配对的消融：**只把服务声明成 `LAT`（BG 显式声明）对 goodput 没有可测影响**——`Cc−C` = −0.01% [−0.29,+0.24]（GET，胜负 10/10）、`Dc−D` = −0.18% [−0.45,−0.02]（GET，p99 4.87→5.35ms），1/2/5ms 三档方向一致；Cc/Dc 相对 A0 的 +1.48%/+1.49% 与无 class 的 C/D（+1.61%/+1.64%）无差别。所以 class 声明是可声明/可审计/可撤销的**契约**，但不是性能开关，见 `evidence/p2/flagship-classrouting-20261023.txt`。自动 apply 仍默认关闭，性能比较仍未开始，v2 无任何性能结论。见 `evidence/p2/auto-apply-pilot-20261022.txt`、`evidence/p2/switch-downtime-20261022.txt`、`evidence/p2/autopilot-suppression-20261022.txt`（含 §9 参数扫描；该文件第 6 节更正了上一版对断言计数与目录引用的一处错误）、`evidence/p2/service-slo-20261022.txt`、`evidence/p2/observer-gap-20261023.txt`。** 主验收系统为 openEuler 24.03-LTS-SP3；Ubuntu 用于开发和辅助兼容验证。旧 EulerPilot、SchedPilot 作为参考资产，按模块迁移。旧前端验收界面不迁入，先用 CLI、结构化状态和可复现实验完成闭环。

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

新版强制 6 个 CTest 必测项无 SKIP、A/B/C liveness、产物哈希和收尾状态断言；
**不包含性能结论**。2026-10-22 已在 SP3 重跑通过，输出见
[acceptance-sp3-strict-20261022.txt](evidence/acceptance-sp3-strict-20261022.txt)；
旧脚本历史输出保留为 [acceptance-sp3-20261021.txt](evidence/acceptance-sp3-20261021.txt)。

SP3 协议 §8 fail-safe 矩阵（故障恢复层，17 条目，需要 root、sched_ext idle 且 pin 根为空）：

```bash
bash experiments/fail_safe_matrix_sp3.sh /root/epv2-build/ep-executord /root/epv2-build/ep \
  /root/epv2-build/ep-sched-loader /root/epv2-evidence/fail-safe-$(date +%Y%m%d)
```

它注入的是控制面与内核故障（崩溃、租约/TTL、非法与越界策略、generation 冲突、双实例、
外部改值、成员迁出、日志盘满、CPU 热插拔、两步写入的第二步失败），逐条判定"回 default fair 后
目标是否继续推进、残余效应是否被内核 map 自证清除"，**不测性能**。2026-10-24 run8 结果
**17/17 PASS、rc=0**（同一天 run4 曾以 S14 FAIL 收尾，该缺陷已修复并重验），见
[fail-safe-20261024.txt](evidence/p1/fail-safe-20261024.txt) §7 与
[P1 退出门槛判定](docs/p1/P1_EXIT_REPORT.md)。

## 2026-10-24 状态

**P1 判定为达成（准出成立）**，逐项判定与不覆盖的边界都在
[docs/p1/P1_EXIT_REPORT.md](docs/p1/P1_EXIT_REPORT.md)：adaptive、心跳、PMU 三条在 v2 上是
UNSUPPORTED 而不是通过；stall 类缺陷未穷尽；越权改 class 条目不会被纠正；崩溃后的 pin 靠
**同实例重启**回收（换 state 目录则无人回收）；本轮四项改动都晚于旗舰矩阵构建，所以 C/D/A1
的数字必须在新冻结构建上重跑才算 P3 结论。P1 达成不等于 P2/P3 达成——门控的收益仍未测出，
P4 的 cgroup Skill 与 eBPF 事件链未开始。

本轮为达门槛改掉四个产品问题：执行服务收到停止信号只退出进程、不回退也不清 pin（新增
`Executor::shutdown()`）；回退后的 class 撤销只信 helper 退出码（改为 `class-get` 读回自证，
pin 残留时把还读得到的片长/vtime/class 一起报出）；外部越权改 cfg 无人发现（每刷新周期读回
一次并写回，实测 5ms/次）；状态目录写不下去时 `ep rollback` 被前置的 `lease.acquire` 挡死
（现授予"仅内存租约"并标 degraded：**拆得掉旧效应、开不了新效应**，`policy.status` 新增
`store_warning` 使存储写失败在恢复之后仍可报告）。另补上"两步写入的第二步失败"的真机构造
（S17：`ROLLED_BACK` + `APPLY_FAILED`、generation 不推进、无残留、可干净重试）。

两条内核事实不是我们能选的：CPU 下线会让本内核直接注销 `eulerpilot`（`hotplug_seq` 变、
`enable_seq` 不变），执行服务 3 次存活判定内检出并补偿回退。

回归：unit 443 条断言、`ctest` 6/6、验收 r25 与 r26 各六轮 rc=0
（`evidence/acceptance-sp3-20261024-r25/`、`-r26/`）。改动全部未提交。

## 设计重点

- SchedPilot 的多队列、唤醒抢占、分类器和实验方法作为起点，先移植静态 C 路径，再验证路由和自适应是否带来增益。
- EulerPilot 的目标解析、Skill、审计与恢复思路保留；控制面重写为轻量 Agent 与唯一特权写入服务。
- 按业务角色与运行特征分别建模；BACKGROUND 来自显式授权，不能凭 CPU 占用猜测。
- SP3 上的真实 sched_ext 闭环是硬门槛；缺能力时只允许标记为降级运行，不算完成赛题核心。
- 以服务 SLO、后台最小进度、静默开销和故障恢复共同约束性能优化。

下一步：旗舰参数已按 pilot 冻结（offered 60000 rps、SLO 5ms、BG 单位 `probe5s` + 下限 140 单位/秒 + 最长等待 50ms、前向 p99 绝对上限 10ms、20 block × A0/A1/A2/C/D、`seed=20261024`、配对 block bootstrap 10000 次，见 `evidence/p2/flagship-params-20261023.txt`），正式矩阵已跑完出结果（20 block × A0/A1/A2/C/D，块内配对 + 10000 次配对 block bootstrap，100/100 块 valid、原始件重算 goodput 与探针自报最大偏差 0.019%）：固定 expert 臂 **C vs A0 goodput@5ms +1.62% [+1.53,+1.82]，20 块全胜**、SET +1.28% [0.83,1.83]，p99 降 48%/32%、p99.9 从 16.4ms 到 8.6ms；**C vs A2 +1.90%（20/0）⇒ 收益不是静态 CPUWeight 倾斜**；**A1（只观测不挂载）比 A0 低 0.82% [−1.07,−0.36] ⇒ 观测栈自身是净成本**；**D 每块恰好切换 1 次，SET 比 C 低 0.27% [−0.48,−0.05]、p99 差 7.6–10.5% ⇒ 这条负载上自动切换没赚回自己的空窗**；BG 相对进度 0.997–1.001、等待 C 3.78ms < A0 5.98ms ⇒ 前台收益没有饿死后台。**阈值敏感性同报且不可省：@2ms 时 C SET −0.17%、D GET −0.00%、D SET −0.57%，收益集中在 5ms 以上远尾**。范围限定：单机单负载单 SLO 设定、跨机与未见负载未做、BG 是"未受损"不是"被优化"、四分类路由未接入控制面故本轮收益只属于 NORMAL→COMP 路径与 BG。见 `evidence/p2/flagship-matrix-20261023.txt`。`fd_passing` 负分支的可测性已解决——但不是"加后门"，而是发现 `SchedPaths` 本来就是合法注入点，直接构造 `fd_passing=true` 的 `SchedSkill` 即可在 unit 层直达该分支（12 条断言），此前"UNSUPPORTED-at-unit"的判断是错的，已在四处文档更正。剩下按顺序是：①门控与"切换延迟代价"都测完了：**行为**上 `D`（上限 1ms）每轮 25 次 `slo denied`、切换被推迟到注入撤掉后（≈52s），`Dng` ≈12s 即切换 ⇒ **门把动作推迟约 40 秒**（且遇到碰巧达标的窗口仍会放行，是延迟器不是否决器）；**代价**上把延迟拆成"单位时间机会差 × 40s"，在可分辨的稳定区间（40k + 8 spinner，n=6）两个 expert 的 goodput 差是 **−0.00% [−0.01, 0.00]**（两臂均 100% offered、p99 1.1–1.3ms）⇒ **40s 延迟的代价 ≈0–30 个请求（上界），即没有证据表明门控延迟造成吞吐损失**；在专家可能拉开差距的过载区（60k + 16 spinner）机器双稳、CI 全跨 0，**该区间仍不可测**。场景覆盖已补齐到：quiet（旗舰：C vs A0 **+1.6%**，无回归）、noisy-cpu、**noisy-mixed 与 phase-change**（新时间线驱动，2×4×60s：A0 在 mix 相 goodput 掉到 **38.96k/39.08k**、p99 **4–5ms**、累计超预算 **0.71%**；D 四相 **≥39.97k**、p99 **≤1.2ms**、超预算 **0.02%**，每条时间线只切换 1 次且 noisy 段保持 `already in place`；恢复无吞吐残留）。**mixed-foreground 已跑首轮**（3 轮 × A0/D：svc-A 30k/5ms + svc-B 15k/2ms + 8 spinner；A0 下 svc-B 只交付 **77%**、p99 4.02、联合 SLO **0/3**；D 下 svc-B **99%**、p99 1.98、联合 **2/3**，唯一 miss 是 2.05ms 擦线；**不能归因给 class 分离**，缺 `D-no-class` 对照）；**cross-machine 记为 UNSUPPORTED**（唯一验收级主机是 SP3，SP4 因 cgroup v1/无 PSI 排除，不伪造跨机结论）。下一步：P1 退出门槛（其余模式 fail-safe、加载场景 SLO/不饿死），以及为 mixed-foreground 补 `D-no-class` 对照；回归基线 `ep-executord bc3a163a…`、`ctest` 6/6、r24 六轮 rc=0；②class 路由已完成配对消融并给出**零结果**（20 block 块内配对：`Cc−C` = −0.01% [−0.29,+0.24]、`Dc−D` = −0.18% [−0.45,−0.02]（GET），两者相对 A0 的 +1.5% 与无 class 臂相同 ⇒ 声明 LAT 不是性能开关，既有收益仍来自 expert 的切片/预算参数）；剩下的是 `llc_affinity=True` 的 CACHE 分档路由、`SP_DSQ_SHARED`（控制面无开关）与能真正让 LAT/CACHE 有区分度的负载；要把性能结论升级成"四分类路由的收益"，必须**带 class 声明重跑矩阵**——已完成的 20 block 是在没有 class 声明的形态下测的，只能算 NORMAL→COMP 路径 + BG 的收益；③stall 类缺陷的其余路径与 LAT/CACHE/BG 的同类压测；④P1 退出门槛（其余模式 fail-safe、加载场景 SLO/不饿死）。回归基线：unit 365 条、`ctest` 6/6、`accept_sp3_rounds.sh` r21 六轮 rc=0（`ep-sched-loader 45bdc3c0…`）。真实顶替已证明在本内核不可构造（第二次 struct_ops 注册被 EBUSY 拒绝）。SP3 已可用；SP4 也已启动（`<SP4-IP>`），但该机 cgroup 为 v1、PSI 缺失，只作可选兼容验证，不替代 SP3。详细状态见 [实施计划](docs/IMPLEMENTATION_PLAN.md)。

## 许可

本项目采用 **GPL-2.0**（2026-10-21 决定），全文见 [LICENSE](LICENSE)。组件级边界见 [NOTICE](NOTICE)：
`sched/` 为 SchedPilot 派生的 GPL-2.0 组件，`third_party/nlohmann` 为 MIT，旧 EulerPilot 仅作参考。
