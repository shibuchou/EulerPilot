# SchedPilot 只读审计：v2 的可复用基础与性能证据边界

审计日期：2026-10-21。来源仓库：`D:\code\Ubuntu\schedpilot`，本地 HEAD `0aeca0cba2b4c9b4d4292847d71018981a04b552`。本次未运行工作负载、未加载 BPF、未修改旧仓库、未访问或修改远端。以下历史结果均属于旧项目，不能写为 EulerPilot-v2 已完成的结果。

## 1. 结论

可迁移的核心是**分类调度数据面、分类器、真实实验流水线和故障测试思路**，不是把旧 daemon 原样放入新 Agent。优先复现静态分类 C，再验证路由/参数自适应的增量。旧实验已经说明：分类 C 获得了大部分目标服务收益，D 的在线参数调整在 Redis/Nginx 上只有小幅增量，在 MySQL 上无明确增量。

旧结果证明的是**混部资源再分配后目标服务改善**：服务和干扰在相同 CPU 集，服务被识别为 LAT，后台任务被配置显式标记为 BG。它不能直接证明全机吞吐提高、任意未知负载自动变好，或 PMU/LLC/学习算法各自有显著贡献。应把这些边界写进新方案，而不是只复述 +181.5%。

两项必须修正的设计前提：

1. SP3 全闭环已有旧证据；不是只有 SP4 可运行。旧团队使用 SP3 源码构建启用 sched_ext 的内核，仅 SP3 迷你实验样本不足以替代 v2 正式矩阵。
2. BASIC 不是 Linux 默认 fair 调度器，旧无干扰实验中它反而比 C/D 损失更多吞吐。不能把“退回 BASIC”当作“消除静默开销”的已验证方案。

## 2. 实验臂与控制的真实含义

实现定位：`bench/abcd_experiment.sh:207–230`。

| 臂 | 实际启动方式 | 可比较的含义 | 不能直接归因的内容 |
|---|---|---|---|
| A | 停止 SchedPilot，系统默认 fair 调度类 | 原生默认基线 | 不能仅按“6.6”认定内部算法与经典主线 CFS 完全相同 |
| B | loader `--mode basic --no-daemon` | shared vtime DSQ 基础 BPF 调度器 | 与 A 差异包含 BPF 调度算法、切片、系统级调度影响；不是单纯“sched_ext 框架开销” |
| C | loader `class` + daemon `--policy off` | 在线分类 + 静态调度参数 | **C 仍有在线感知/分类**，只是参数调整关闭；C−B 还包含 daemon/PMU/特性等差异 |
| D | loader `adaptive` + daemon `--policy on` | 在线分类 + 有界参数调整 | D−C 才更接近参数自适应的增量，不代表 LLM/学习路由收益 |
| d-no-pmu | D + `pmu_classify=0` | PMU 分类消融 | 不是关闭全部观测/关闭整个 PMU 采样开销的充分证明 |
| d-no-llc | D + `llc_affinity=0` | LLC 路由/迁移惩罚消融 | VM 的 LLC 退化检测已可能禁用该路径；零差异不能证明该特性无价值 |
| d-no-bg | D + `bg_contain=0` | 关闭 BG vtime 惩罚 | **仍有 LAT 队列优先**，因此不等于公平性对照 |

正式场景 v3：服务和干扰同 `SERVER_CPUS=0-3`，客户端 `4-7`；4 个 CPU 干扰 worker + 2 个内存干扰 worker，每个内存配额 `1G`。元数据记录 10s 预热、60s 测量。`abcd_experiment.sh:283–289` 轮转臂顺序，减少热/时序漂移，但这是确定性轮转，不能称为随机试验。

`configs/redis.conf:11–13` 明确将 `stress-ng` 配置为 BG；`schedpilotd.cpp:1098–1104` 为其写入 `SP_SRC_EXTERN`、置信度 1.0。由此必须区分：**workload 行为分类（LAT/COMP/CACHE/NORMAL）是推断，BG 是外部授权角色**。新项目不要宣称自动认出“谁是不重要任务”，更不要仅凭高 CPU 使用率把未授权进程降为 BG。

默认旗舰配置没有开启 `bg.cpu_pool`，但新版本具备显式设置亲和性的可选功能（`schedpilotd.cpp:968–1013`）。新矩阵必须把纯调度器实验与 cpuset/cgroup/affinity 联合资源控制实验分开，逐轮存储实际 affinity、cgroup 配置和 CPU 时间。

## 3. 历史数字复核

本次以四组本地 `per_run.csv` 独立计算逐轮 `100×(arm/A−1)` 的均值，与摘要一致；CI 引自对应 `summary.md`，本次未重新验证所有原始逐请求输出或 CI 的分布假设。

| 历史实验 | 固定版本与样本 | 配对 QPS 均值变化 | 证据解释 |
|---|---|---:|---|
| formal-4 C/A | `be962b8`，20×60s | +167.3933%，摘要 CI [+162.31%, +172.48%] | 分类静态策略已获得大部分收益 |
| formal-4 D/A | 同上 | +169.8113%，CI [+164.42%, +175.20%] | D 相对 C 增量小；必须直接比较 D/C 的配对区间 |
| formal-5 D/A | `65c44854548a6dc0f305a3ee6022f758418b0272`，20×60s | +181.4908%，CI [+174.15%, +188.83%] | 此批仅 A/D，不能补作同构建 C/D 消融结果 |
| nginx-7 C/A | `3fdaf80`，20×60s | +167.8057%，CI [+141.06%, +194.56%] | A 波动较大；不要混同中位数比值 +137.1% |
| nginx-7 D/A | 同上 | +169.8179%，CI [+142.58%, +197.06%] | D/C 名义接近，不能吹成自适应大幅提高 |
| noif-2 B/A | `be962b8`，10×60s | −5.6702%，CI [−7.15%, −4.19%] | BASIC 有系统性无干扰吞吐成本 |
| noif-2 C/A | 同上 | −3.2038%，CI [−4.00%, −2.41%] | p99 改善但吞吐降低 |
| noif-2 D/A | 同上 | −3.2783%，CI [−4.52%, −2.04%] | p99 配对 −11.04%；代价收益都要披露 |

`docs/04_test_report.md:443–458` 给出归因边界：A 目标 Redis CPU 时间占比约 16.7%，D 约 49.9%；约 3 倍目标 CPU 换来约 2.7 倍 QPS。进程 CPU 分母与测量区间必须严格匹配；该表的 CPU 列和其他 perf 指标并非完全同一采集来源。它支持“优先保障目标服务”的叙事，不支持“每个 CPU 周期效率提高 181%”。

旧失败也有参考价值：formal-1 轻干扰场景未达到目标收益；formal-3 因 BG 10ms 切片和队列顺序造成回归；nginx-5/6 的 D 臂后来发现干扰残留/启动问题而作废。保留这些结果和修正，不把只保留最佳轮次作为新实验策略。

## 4. SP3 事实

证据：`docs/04_test_report.md:476–494`、`evidence/sp3-vm/README.md`、`evidence/sp3-vm/build.log`、`evidence/sp3-vm/sp3-hello/`。

- 旧环境 openEuler 24.03-LTS-SP3，发行内核 `6.6.0-132.0.0.111.oe2403sp3` 未启用 sched_ext。
- 使用 `/usr/src/linux-6.6.0-145.3.34.165.oe2403sp3.x86_64` 源码，开启 `CONFIG_SCHED_CLASS_EXT` 与 `CONFIG_EXT_GROUP_SCHED` 后重建 `6.6.0-schedpilot-sp3`，有全量构建成功记录。
- SP3 Redis A/D 2×20s：配对 QPS +128.98%、p99 −30.62%。**只能作为能运行的证据**，不能作为正式性能结论。
- 这些事实反驳“SP3 一律无 sched_ext 代码、必须手工回合主线 6.12”的笼统断言；仍需核实 v2 实际选择的源码包/分支、kernel config、BTF/kfunc ABI、编译器、PMU 和二进制哈希。
- 赛题写 SP3 就以 SP3 为主交付与正式实验环境；是否允许自定义内核/VM 是需要维护方明确的规则问题，不能自行改成 SP4 作为正式交付。

## 5. 可复用模块与迁移前修正

| 模块 | 代码位置 | 可复用内容 | 迁移前要求 |
|---|---|---|---|
| BPF 队列路径 | `bpf/scx_schedpilot.bpf.c:304–409` | 默认 select CPU、空闲直投、LAT/COMP/CACHE DSQ、受限抢占 | 加可靠公平性预算，核对线程身份与内核 ABI |
| 调度计数 | 同文件 `244–273`、`414–482` | runtime/wakeups/switch/delay 计数 | 分清 event-mean 与 tail；热点 TGID 原子更新的 cacheline 成本需测 |
| cache locality | 同文件 `434–446`、`490–507` | 迁移惩罚上限、LLC 本地优先再窃取 | VM 无真实 LLC 代表性，在物理多 LLC/NUMA 环境单独测 |
| 分类器 | `daemon/classifier.cpp:34–190` | EWMA、非对称滞回、CUSUM 相位检测 | 修正 confidence 对应当前 raw 类而非滞回保留类的问题；缺失/过期数据退回 UNKNOWN/NORMAL |
| PMU 采样 | `daemon/pmu_sampler.cpp:61–91,123–261` | 每 TID 事件组、动态线程扫描、multiplex scaling | 验证 inherit 与逐 TID 双计数风险；runtime=0/覆盖率低样本无效；generic cache events 不能直接命名 LLC |
| 生命周期管理 | `loader/scx_schedpilot.c:373–399` | BPF link 生命周期、UEI、进程退出卸载 | pin 所有权/旧 map/boot_id+generation；禁止未经检查 `unlink` 他人 pin |
| 实验控制 | `bench/abcd_experiment.sh`、`run_redis.sh`、`interference.sh` | 轮转臂、预热、存活断言、错误标 INVALID、元数据 | 增补资源等价对照/随机种子/公平性/SLO过载/清理证据 |
| 分析器 | `bench/analyze_results.py:181–209` | 逐轮配对、区间、原始 CSV | 公开过滤原因，配对非随机不能消除全局漂移，CI/重复性检查 |
| 故障验证 | `tests/test_fault_injection.sh`、`test_soak.sh` | Agent/loader 故障、持久稳定性思路 | 重新为新 IPC/事务/epoch 边界写有意义的测试 |

第一批迁移只包含 BPF/ABI、loader、分类器及对应分类单测、实验解析器，并固定来源 commit/hash/SPDX。daemon 拆成 Observer、Classifier、Router、Actuator；生产只保留一个有效写者，旧 schedpilotd 仅独立实验臂运行。PMU 与 LLC 默认可探测降级，不应成为首个闭环的阻塞依赖。

## 6. 必须处理的数据面和观测风险

### 6.1 公平性与饥饿

`scx_schedpilot.bpf.c:520–530` 明确先消费 LAT，且旧抗饥饿守卫被移除。无限 LAT 可持续压制 COMP/CACHE/BG。BG 只共享 COMP 队列，BG vtime 惩罚不能为整个非 LAT 队列保证服务。新设计应采用服务窗口 CPU 时间预算/加权轮转 + 最大等待触发，保证非 LAT 和未知任务最小进展；优先队列仍受系统默认 RT/DL 等调度类的约束。需要连续 LAT 饱和、恶意高 wake、类别抖动、多租户互扰压力测试，并记录最差等待和后台完成率。单靠 2ms slice 不能给出真实 p99 上界。

### 6.2 心跳失效不等于完整配置回退

`effective_flags():129–148` 仅在 ADAPTIVE 心跳失效时返回静态 flag 组合；`pick_slice():151–169` 仍读取原 cfg 参数，`stopping():476` 还直接读 `c->flags` 决定 BG 惩罚。CLASS 模式未统一施加心跳失效判断。新代码要在一次读取中构造完整 `effective_policy`，所有 hook 使用同一安全快照，覆盖 mode/flags/slices/weights/targets，不能只换 flag 后宣称全部安全默认值生效。类映射 5s TTL 与心跳阈值是两个阶段，要分别验证。

### 6.3 标识与 map 所有权

class_map/分类器按 TGID 键控，未看到 `start_time`/pidfd 复用校验。PID 快速复用时 TTL 内旧分类可能误用于新任务。新 `TaskIdentity` 应包含 boot_id、TGID/TID、starttime、cgroup identity；BPF 可用 task storage 生命周期消除最危险的复用路径。旧 loader `pin_map_alias():112–121` 先 unlink 目标 pin，不能直接复用到多实例环境。以实例锁、owner token、generation、ABI/boot 校验拒绝双写。

### 6.4 PMU 质量

`PERF_COUNT_HW_CACHE_REFERENCES/MISSES` 是 generic cache events（`pmu_sampler.cpp:63–68`），不是跨微架构保证同义的 LLC references/misses；旧 MPKI 应称 cache MPKI 并记录 event mapping。`fill_attr():31` 设置 inherit=1，同时 `ensure_tgid` 对每个现存 TID 建组，新线程场景需要验证继承计数是否重复；旧单进程 Redis 正结果不能消除多线程负载风险。`sample():232–247` 对零 running delta 未明确拒绝，`read_group():183–190` 也未严格验证完整事件数和全部字节；新采样器必须丢弃不完整/低覆盖/计数器回绕样本，记录采样覆盖率和质量位。

### 6.5 亲和性变更恢复

可选 BG CPU pool 直接改每 TID affinity；当前进程退出尾部 `schedpilotd.cpp:1223–1225` 未看到集中恢复全部保存亲和性的流程。不能把 Agent 崩溃后的 BPF fail-open 等同于用户态外部副作用恢复。新执行器需持久化 before-image、实例身份，启动时对账；kill -9 后由独立受限 helper/监督进程恢复。不能仅在正常 target 消失时恢复。

## 7. v2 重新实验建议

1. **同一 SP3 自定义内核、同一构建**重跑 A/B/C/D，先使 C 闭环和无饥饿，再增加规则路由 R；learned router 最后加入。两者必须和最强静态专家比较，而不只和默认 fair 比。
2. 建立三类对照：默认 fair；调过 `cpu.weight`/nice 的 fair（同目标重要性、同 affinity/cpuset）；同资源约束的 scx/SchedPilot 静态策略。后者用于剥离“资源多分给目标”的解释。
3. 同时报告目标 goodput@SLO、p99、后台完成量/slowdown、全机加权吞吐、每核 CPU 时间、Jain 或明确的最低份额约束、最差运行等待。目标性能提升须满足后台最低进展门槛。
4. 混部强度从无干扰、轻、中、重到阶跃变化；固定目标进程身份、授权 BG cgroup。未管理的未知 CPU 任务和多个高优先目标必须纳入测试。保留所有失败、超时、watchdog 脱离轮次，不能静默滤掉。
5. 负载由 Redis 单服务扩展到 Nginx/MySQL 与一个未用于调参的 workload；客户端隔离，记录客户端 CPU/吞吐饱和，增加固定到达率或开环压测验证尾延迟与过载，避免闭环 QPS 上升掩盖尾部问题。
6. 每场景先 pilot 锁定参数，再预注册正式参数、SLO、主指标、最低有意义提升（例如 goodput ≥10% 且 95% CI 下界>0）和 quiet 回归容忍度（例如 ≤3%）；这是内部工程目标，不是赛题明示指标。正式 20 个配对 block ×60s，至少两次独立启动/日期复现，实际成本允许再扩充。
7. 使用带种子的平衡臂顺序、稳定化/冷却与每臂 clean-state；路由器学习状态只能在声明的训练阶段更新，测试场景/trace 不参与选择。需区分在线冷启动、warm router 与事后 oracle；报告 regret、切换损失、相位响应时间。
8. 开销用一致时间窗口测 Agent CPU/RSS、BPF hook runtime、map 更新次数、cacheline 热点、控制频率（100ms感知不代表100ms必须切策略）。quiet 是否选择原生 fair/可选部分切换须按目标内核能力探测；BASIC 只作为可测退化候选，不预设零开销。
9. 离线 trace replay 用于决策一致性、schema/预算验证，不足以证明线上性能或硬件时序安全；最终效果由真实内核闭环与对照实测判定。

## 8. 本次验证范围与待办

已完成：只读代码/报告/四组元数据复核，四组 QPS 配对均值独立复算，SP3 历史构建与迷你实验路径确认。

未完成：未重新运行任何旧实验；未验证完整原始归档内容与哈希；未核查新目标机和实际内核源树；未证明任何 v2 性能提升或公平性保证。下一步首先在 SP3 固定内核/ABI/资源预算，然后迁移最小 C 模式并跑 A/B/C 的干净 pilot。
