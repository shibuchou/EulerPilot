# 实验状态

实验协议位于 `docs/EXPERIMENT_PROTOCOL.md`。这里保存 pilot 后冻结的机器、场景、随机种子、阈值和运行计划。
目前的结果只有八类，且都不是性能结论：

- `abc_nostall.sh`：**liveness**，三臂均推进、状态干净（`evidence/p1/abc-nostall-20261021.txt`）。
- `auto_apply_pilot_sp3.sh`：自动 apply 的授权与控制权移交行为，SP3 实测
  “交接前不动 / 随路由切换 / 收回租约即停”三条属性（`evidence/p2/auto-apply-pilot-20261022.txt`）。
  它量化的是租约语义，不是性能，也不覆盖切换瞬间的停顿。
- `auto_apply_downtime_sp3.sh` + `sched_switch_probe.c` + `analyze_switch_downtime.py`：
  自动切换的空窗时长与唤醒迟到毛刺（`evidence/p2/switch-downtime-20261022.txt`）。
  它测的是切换代价，不是性能收益。
- `auto_apply_suppression_sp3.sh`：切换抑制的**逐门关隔离重放**。同一个负载剖面、同一套断言，
  用 `EXPECT_GATE=dwell|candidate_hold|budget` 把另外两关放到 CLI 下限 1000ms，只留一关能挡住切换；
  判定读执行服务自己写进门状态里的数值（`in place Nms` / `routing target stable Nms` / `switch budget N/M`），
  不用外部时钟相减。三轮结果都在 `evidence/p2/autopilot-suppression-20261022.txt` 第 7 节，
  失败的两次（`--auto-dwell-ms 0` 被参数校验拒绝、时间线整列为空）也归档在 `evidence/p2/`。
  它测的是"会不会反复切换"，同样不是性能结论。同一脚本换 `DWELL_MS/CANDIDATE_HOLD_MS/BUDGET_MAX`
  即参数扫描，四臂（loose/medium/默认/strict）结果在 `evidence/p2/suppression-sweep-*` 与
  该证据文件第 9 节：这条剖面自身自带约 36s 回落迟滞，抑制的可测代价是响应延迟。
- `auto_apply_slo_feedback_sp3.sh` + `service_load_probe.c`：服务级 SLO 反馈的端到端闭环。
  探针量两件事——前台 1ms 周期唤醒的 p99 迟到、后台在固定等待之间完成的工作量与最大 overshoot；
  脚本用租约把"基线测量"锁在切换之前（否则升档会落在测量窗口里），换租约后再由真实窗口解析切换后果，
  并用两个注入窗口验证"单窗只疑、双窗才判"。后续加了分辨率护栏：策略要求的敏感度若不大于
  实测窗间抖动，比较类判定直接报 `not run` 而不是当作通过。全部运行（含失败与设计推翻）在
  `evidence/p2/service-slo-20261022.txt`。它验证的是反馈机制，不是性能收益。
  同一条线的后续两件事：`service_observer_sp3.sh`（常驻观测生产者）与
  `auto_apply_slo_ceiling_sp3.sh`（策略声明的绝对前向上限在实测超限时拒住自动通道），
  证据 `evidence/p2/service-slo-ceiling-20261023.txt`。
- `service_watchdog_sp3.sh` + `service_observer_gap_sp3.sh`：断供的**消费者侧**可见性。前者只读
  `policy.status.service`，对九类事件告警（`STALE_EPISODE/STALE_MEMORY/STALE_NOW/REJECTIONS/NO_EVIDENCE/PRODUCER_SILENT/ACCOUNTING/NO_CONTROL_PLANE/BAD_REPLY`），
  逐 poll 落 CSV、任何违例即 rc=1；后者是真的把生产端 kill 掉再重启：健康阶段必须 0 违例（否则告警没价值），
  断供阶段必须让 hold 报出 `SERVICE_OBSERVATION_STALE` 而不是被钉死的 dwell，重启后活观测恢复新鲜但
  `observation_max_age_ms/stale_checks` 仍留着断供记录。用法都要 `<ep-executord> <ep> <ep-sched-loader> [out-dir]`（看门狗是 `<ep> <socket> <out-dir> …`），
  需 root 与空闲 sched_ext，手工运行、不进 ctest。证据 `evidence/p2/observer-gap-20261023.txt`。
  顺带量到生产端节奏约束：一个周期耗时 `window + interval`，而新鲜度判据是 `age ≤ 2×window`，
  两者相等时健康机子也会闪断——协议 §5.1 第 6 条据此定死。
- `slo_noise_floor_sp3.sh`：量**反馈信号本身的分辨率**（不碰 sched_ext、不碰执行服务）。
  同一状态下重复取 5/15/30s 的前台与后台窗口各 5 次，排除首窗作预热后看相邻窗的相对变化。
  结论：p99 的 CV 3.8–20%、且 30s 窗比 15s 窗更吵（长窗采到的是变化后的世界），
  背景进度相邻窗变化 ≤1%。因此在线口径是"窗口 15s、比较类判定须先声明分辨率、
  前向延迟暂时只能用绝对量"，写进 `docs/EXPERIMENT_PROTOCOL.md` §5.1。
  数据 `evidence/p2/slo-noise-floor-20261022/noise.tsv`。
- `pilot_matrix_sp3.sh`：实验矩阵的 pilot（协议 §6.1）——3 块 × A0/C/D，种子与真实臂序入档，
  redis GET/SET 分开报、BG 导出真实完成量、逐块 manifest + 文件哈希 + 残留检查。
  它的价值是把 harness 的谎翻出来：窗长只有 0.4s、块内切换与测量窗同量级、goodput@SLO 算不出、
  客户端未标定、缺 A1/A2。结论写在 `evidence/p2/pilot-matrix-20261023.txt`，**不进正式表**。
- 负载测量件（是量具，不是结果）：`redis_openloop_probe.c` 按固定到达率开环打 Redis 并写逐请求原始件；
  `redis_rate_calibration_sp3.sh` 标定发生器自身瓶颈；`redis_matrix_sp3.sh` 跑 A0/A1/A2/C/D 五臂
  （挂载与 CPUWeight 都读回证明、BG 与在线门控同口径、flock 互斥）。pilot 版保留作对照。
  数据 `evidence/p2/matrix-harness-20261023/`，97MB raw 延迟件留在 SP3、逐块哈希已入库。
- `flagship_param_pilot_sp3.sh`：冻结旗舰参数（协议 §6.1/§5 要求 BG 下限与最长等待在 pilot 后预注册）。
  阶段 1 用 A0 扫 offered 率，阶段 2 在候选率上跑挂载臂；配合 `analyze_openloop_raw.py`
  **从逐请求原始件**重建分位数与"每条候选 SLO 阈值下的 good%"，一次运行同时回答"选哪条阈值"。
  这批 pilot 的价值是发现数据面缺陷（下表），其 C/D 数字在修复前不可用。
- `stall_watch_sp3.sh`：不测性能，只测**存活**——挂载后用配料开关（`LOAD_REDIS`/`LOAD_BG`）
  分别跑"不加载 / 仅后台 / 仅服务 / 全部"，每秒轮询 `sched_ext/state`，dmesg 增量里找
  `runnable task stall`，并在挂载期间用 bpftool **只读** dump `stats` map。
  `enq_comp == dsp_comp` 这条计数是排除"DSQ 没人排空"假设的直接依据。
  见 `evidence/p1/stall-runnable-20261023.txt`。
- `Cc`/`Dc` 臂（`redis_matrix_sp3.sh` 内）：与 `C`/`D` 共用目标、expert、参数、数据集与 cgroup 放置，**唯一差别**是策略多声明 `class=LAT`（服务）与 `class=BG`（后台）。每块写 `classes.proof`（apply 回报的 declared/effective 必须符合预期，否则该块 invalid）与 `cgroup.proof`（从 `/proc/<pid>/cgroup` 读回放置）。这批臂已给出零结果：声明 LAT 不改变 goodput，自适应臂里还略负，见 `evidence/p2/flagship-classrouting-20261023.txt`。
- `mixed_foreground_sp3.sh`：**协议 §4 的双前台竞争**——两个 redis 实例（30k/5ms 声明 LAT、15k/2ms 声明 COMPUTE）各自一个 cgroup，外加 8 个 spinner 制造稀缺。判据 `met` = 该服务 ≥99% 请求在预算内，`joint` = 两者都 met。**注意**：空载时两个服务都轻松达标（smoke 第一版什么也没测到），竞争必须靠人为稀缺；且 D 与 A0 的差别含调度器/expert/观测栈，不能归因给 class 分离。见 `evidence/p2/mixed-foreground-20261023.txt`。
- `phase_change_sp3.sh`：**协议 §4 的 phase-change 时间线**——quiet→noisy-cpu→noisy-mixed→quiet，一条连续时间线里每相单独一个 probe 与原始件（所以可以按相重建 goodput/p99），每 2s 轮询并把切换归因到相。臂 A0 是裸服务（不启动执行服务），D 是执行服务 + autopilot。注意：观测者只在 D 侧运行，所以这是"系统 D 对裸 A0"，且 **PSI 不可跨臂比较**；要单独测调度器需再加 C（固定 expert、无观测者）臂。见 `evidence/p2/phase-change-20261023.txt`。
- `expert_gap_sp3.sh`：**固定 expert 的静态对照**，用来量"注入条件下两个 expert 的单位时间差"（门控延迟代价的另一个因子）。两个臂都在测量前已应用并静置，窗内无切换、无 autopilot；每轮交替顺序，并断言 `active_expert` 等于声称值。注入强度/offered 率是环境变量，因为**过载区间在这台机器上是双稳的**（同一臂 p50 在 2.3–14.4ms 间跳），只有稳定区间能给出可分辨的对照。见 `evidence/p2/expert-gap-20261023.txt`。
- `gate_injection_sp3.sh`：**让服务门控真正触发**的场景（基线不交租约 → 注入争抢并静置 → 交租约边测 45s → 撤载）。同一轮里 `D` 门控生效、`Dng` 阈值不可 bind；驱动**内置前置断言**（D 必须出现 `slo denied` 且不动作、Dng 必须动作），条件没成立直接把 `CHECK_*=no`，避免把"条件不存在"读成"门没用"。注入强度与邻居权重是环境变量（`SPINNERS`/`SPINNER_NICE`/`WAKERS`/`GATE_CEILING_MS`），不同强度会改变机器均衡点。见 `evidence/p2/gate-injection-20261023.txt`。
- `class_route_soak_sp3.sh`：**接入 class 声明之后的存活压测**。用本运行自己创建的三个 cgroup
  分别承载 LAT（redis 服务进程）、CACHE（两个 64MB 工作集的内存密集进程）、BG（与在线门控同单位的
  生产者），由**策略**声明这些 cgroup 的档，执行服务仍是 class_map/cfg 的唯一写者。判据只有两条：
  路径确实被喂（`stats` 只读 dump 里 `enq_lat/enq_cache` 非零）与调度器仍然活着（每秒轮询
  `sched_ext/state` + dmesg 增量里 0 条 `runnable task stall`）。不测性能。
  注意坑：把进程放进 cgroup 必须写 `$BASHPID`，子 shell 里的 `$$` 是调用方 shell 的 PID，
  用错会让某个档位静默零流量，而计数器上"没流量"和"没接上"同形。
  见 `evidence/p1/class-route-20261023.txt`。
- `fail_safe_matrix_sp3.sh`：**协议 §8 的 fail-safe 矩阵，故障恢复层**。17 个条目一次跑完，
  注入的是控制面与内核故障而不是负载：挂载中 SIGKILL 执行服务、sched-ref 下 SIGKILL loader、
  租约到期（心跳停止的等价路径）、策略 TTL 到期、非法/越界/类型错误的策略、generation 冲突、
  第二实例、越权改 cfg 与 class、成员迁出、状态目录灌满（4MiB tmpfs）、CPU 下线、
  **两步写入的第二步失败**（把 `ep-schedctl` 换成只拒绝 `class-set` 的包装器，并记录每一次调用）。
  每条判三件事：`sched_ext` 是否回到 disabled、目标进程是否继续推进（BG 计数 + 前台 utime）、
  **残余效应是否由内核 map 自证清除**（`class-get` 读回、pin 目录是否消失）。
  判定按场景隔离：每场景开头记 pin 目录基线，只判"本场景新增/遗留自己的目录"，
  否则一个场景的泄漏会把其后所有场景误判成失败（run1 就是这样误判了 12 条）。
  四处坑记在证据里：`policy_ttl_ms` 低于 schema 下限 10000 时策略本身就被拒，当不了计时器；
  越权写 `lat_slice_ns` 若低于 helper 自身下限 250000，测到的是参数校验而不是漂移检测；
  场景内部不能 umount 自己挂的 tmpfs（守护进程还开着 socket/lock/journal，`umount` 必然 EBUSY，
  会把全绿的一轮判成 rc=1），必须由 EXIT trap 在回收进程之后卸载；挂载之后的第二步失败契约是
  `ROLLED_BACK` + `error.code=APPLY_FAILED`（补偿成功、generation 不推进），不是 `REJECTED`
  ——run7 因此失败，run8 通过。
  收尾断言 pin 根为空并按 `state.json` 反查泄漏目录属于哪个场景；2026-10-24 run8 = 17/17 PASS。
  **不测性能，不产出任何百分比**。见 `evidence/p1/fail-safe-20261024.txt`。
- 分析件（不产数据，只重建）：`analyze_openloop_raw.py`（原始件 → p50/p95/p99/p99.9 +
  任意阈值的 good%，并核对 `records_vs_completed`）、`analyze_matrix_blocks.py`
  （块内配对比较 + 配对 block bootstrap 95% CI + log-ratio 敏感性 + 胜负轮数 + BG 相对进度，
  并按协议校验"原始件必须能重建表"：原始记录数与摘要 completed 不一致就把该块标出来）。
- `flagship_matrix_sp3.sh` + `flagship_params.json`：正式矩阵运行器。参数只从 JSON 读，
  运行前把参数文件自身的 sha256 写进 `protocol.json`（预注册可事后核对），结束后产出
  `attempts.jsonl`（含失败块与原因）、`per_run.csv`、`comparisons.csv`。
  它拒绝在 `sched_ext` 非 idle 或参数文件缺失时启动。
- 数据面公平性探针在 `tests/fairness_sp3.sh`（+ `fairness_probe.c`）；顶替探针在
  `evidence/p1/takeover-sp3.sh` 与 `evidence/p1/takeover-thirdparty-sp3.sh`。

正式实验矩阵仍未开始，这里没有任何以旧结果填充的示例成绩。
