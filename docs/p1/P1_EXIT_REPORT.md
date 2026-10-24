# P1 退出门槛判定报告

日期：2026-10-24（同日修订两次，见 §7）。判定对象：`docs/IMPLEMENTATION_PLAN.md` 阶段表中
P1 的退出门槛（"没有双写、TID/实例正确、所有模式 fail-safe、单域崩溃恢复通过、A/B/C 无失速；
quiet/后台初测"）与第 120 行补加的"加载场景的 SLO/无失速"。
主要证据：[fail-safe 矩阵](../../evidence/p1/fail-safe-20261024.txt)
（`evidence/p1/fail-safe-20261024/` = run8，17 条目全 PASS；`-run4/` 保留 S14 FAIL 的原始现场，
`-run6/` 是修复后、加 S17 前的中间态）。

## 1. 结论

**P1 判定为达成（准出成立）**，7 项门槛都有真机或可复核证据；判定附带下面这些不可省略的
边界，任何一条都不被"P1 通过"这句话覆盖：

- **不是"三种模式都能跑"**：v2 只实现 basic/class，adaptive 在 apply 阶段被拒（这本身是
  它的 fail-safe 形式）。协议 §8 要求"心跳在 CLASS 和 ADAPTIVE 都失效安全"，在 v2 上
  **无法满足**，记为 UNSUPPORTED by design。
- **不覆盖 PMU**：v2 没有 PMU 消费者，"PMU 拒绝"无路径可测（UNSUPPORTED）。
- **不穷尽 stall 类缺陷**：17 条目里没有"排队任务卡死"的构造。
- **性能边界照旧**：旗舰矩阵数字来自修复前的构建；本轮新增的"每刷新周期一次 cfg 读回"与
  "退出时补偿回退"发生在其后，**C/D/A1 未在新构建上重跑**，P2/P3 结论必须在冻结构建上重出。
- **P1 达成不等于 P2/P3 达成**，也不意味着跨机验证（唯一验收机是 SP3，SP4 因 cgroup v1/
  无 PSI 被排除）。

## 2. 逐项判定

| 门槛 | 判定 | 层级 | 证据 | 边界（不证明什么） |
|---|---|---|---|---|
| 没有双写 | **达成** | 单元+真机集成+故障矩阵 | S11（第二实例 `another executor instance holds .../executor.lock`，且未挂载）；`evidence/p1/takeover-thirdparty-20261022.txt`（第三方 `schedpilotd` 在场时拒绝挂载；第二次 struct_ops 注册被内核 EBUSY 拒绝）；`evidence/p1/sp3-instance-pin.txt`（pin 按实例命名空间隔离） | 只保证 **v2 自己不产生第二写者**。`ep-schedctl` 是 root 可执行的旁路工具，任何 root 进程都能写 cfg/class_map；S12 实测越权改 cfg 会被纠正，**越权改 class 条目不会被纠正**（逐条读回要按分类任务数付费，落在毫秒级刷新预算里不成立） |
| TID/实例正确 | **达成（范围如实收窄）** | 单元+真机集成+矩阵 | `evidence/p1/sp3-pidfd-held.txt`、`sp3-pid-target.txt`、`sp3-cgroup-id.txt`（cgroup 用 kernfs inode+device+cgroup_id+boot_id；pid 用 pidfd+starttime，fd 持有到事务结束）；S04（同实例重启回收自己的 pin，异实例不越权清理）；S11（拒绝他人 pin） | 线程级目标不做：class_map 以 tgid 为键，S16 记为超出范围。真机 PID 复用 **SKIP**：pidfd 已在结构上排除"回收的 pid 在同一绑定下被重新解释"，去调 `kernel.pid_max` 逼复用测的是内核而不是我们的代码 |
| 单域崩溃恢复通过 | **达成** | 故障恢复层真机 | 进程崩溃：S04（挂载中 SIGKILL 执行服务→内核立即摘除→同实例重启 `recover_orphan` 扫 pin、`degraded=false`）、S05（SIGKILL loader→3 次存活判定内 `auto fallback: scheduler lost`）；服务退出：`Executor::shutdown()`（本轮修复，run2 曾实测到退出留下 5 个 map pin）；存储故障：S14（ENOSPC 下授予"仅内存租约"完成拆除、新效应被 `RECOVERY_REQUIRED` 拒绝、`store_warning` 可报告）；WAL 状态机 unit 矩阵与 `evidence/p1/wal-hardening-20261022.txt`；根因更正见 `evidence/p1/enable-seq-publish-20261022.txt` | "崩溃不证明已穷尽"；崩溃后 pin 由**同实例重启**回收，换 state 目录则无人回收（运维约束） |
| 所有模式 fail-safe | **达成（口径：v2 可达模式）** | 故障恢复层真机 | basic：S02/S07；class：S01/S04/S05/S06/S12/S13/S15/S17；adaptive：S03 在真实 RPC 上 `REJECTED` 且零副作用；两步写入的第二步失败：S17（`status=ROLLED_BACK`、`error.code=APPLY_FAILED`、generation 不推进、无残留、可干净重试） | 见 §1：心跳与 PMU 两条 UNSUPPORTED；热插拔在本内核会注销调度器（S15），v2 的行为是检出并补偿，不是"扛住" |
| A/B/C 无失速 | **达成** | 真机+性能矩阵 | `evidence/p1/abc-nostall-20261021.txt`（fair/basic/class 三臂均推进、状态干净，明确不是性能比较）；`evidence/p1/stall-runnable-20261023.txt`（`runnable task stall` 根因与修复，269/269 poll enabled）；旗舰矩阵 C/D 40/40 块结束仍 `enabled`、dmesg 0 条新 stall | 不声称 stall 类缺陷已穷尽；`llc_affinity=True` 与 `SP_DSQ_SHARED` 未压测 |
| quiet/后台初测 | **达成（初测口径）** | 性能层（协议 §6.2） | `evidence/p2/flagship-matrix-20261023.txt`：60k rps 开环、20 block × 5 臂；BG 相对进度 0.997–1.001、BG 等待 C 3.78ms < A0 5.98ms；A1（纯观测栈）−0.82% | 单机单负载单 SLO；BG 是"未受损"不是"被优化"；B 臂（BASIC scx + 同等观测）未进正式矩阵 |
| 加载场景的 SLO/无失速 | **达成（价值未测出，如实分开）** | 性能+故障恢复层 | 无失速同上；SLO 链路 `evidence/p2/service-slo-20261022.txt`、`service-slo-ceiling-20261023.txt`（绝对上限触发时自动通道 0 次切换、饥饿注入 `BG_PROGRESS_FLOOR` 拒动）、`observer-gap-20261023.txt`（断供可报告）；场景 `phase-change-20261023.txt`、`mixed-foreground-20261023.txt` | **门控收益仍未测出**：`gate-injection-20261023.txt` 只证明门是"延迟器不是否决器"（推迟约 40s），`expert-gap-20261023.txt` 证明代价只在稳定区间可分辨（≈0–30 个请求上界），过载区间机器双稳**不可测** |

## 3. 为达成本轮门槛而改掉的产品问题

1. **执行服务收到停止信号只是退出进程**：只关 socket、删 pid 文件、返回；调度器靠进程退出
   顺带关 link fd 被内核摘掉，但没人 unlink pin、也没人走回退事务（run2 实测到 S12/S15 各留下
   一个含 5 个 map pin 的实例目录）。新增 `Executor::shutdown()`：停监控线程后若仍有生效效应，
   走同一条补偿回退（含 `verify_rolled_back`）再收尾。
2. **回退后的残余效应读回**（协议 §8"不能只看 cfg.flags"的落点）：撤销必须由 map 自证——
   `class-del` 之后 `class-get` 报"不存在"才算撤销；`verify_rolled_back` 通过语改为
   "…no class or cfg outlives the unpin"，pin 残留时把还读得到的片长/vtime 与还活着的 class
   条目一起报出；孤儿回收从 `sched-runtime.json` 的 `classified` 重建受管集合。刷新路径**不**
   开逐条确认（成本随分类任务数增长）。
3. **cfg 漂移再断言**：每刷新周期一次 map 读回（实测 5ms），不一致即写回并打印
   `[sched-skill] cfg drift corrected: …`。
4. **状态目录写不下去时回退被挡死**（S14 原 FAIL）：`lease.acquire` 现在在存储失败时授予
   "仅内存租约"（`lease_persisted:false` + `warning`）并把服务标为 degraded——**方向不对称**：
   不可持久化的租约足以拆除已有效应（回 default fair 永远是安全方向），但新效应被
   `RECOVERY_REQUIRED` 拒绝；`policy.status` 新增 `store_warning` 使存储写失败在恢复后仍可报告。
   契约扩展记在 `docs/p1/ABI_FREEZE.md` §6 第 8 条。

## 4. 本轮修掉的测试件缺陷（不记录就等于没发生）

- S07 最初用 `policy_ttl_ms=6000`，低于 schema 下限 10000，策略在成为计时器之前就被拒。
- S12 最初用 `lat_slice_ns=111111` 扮演越权写，低于 helper 自身下限 250000，测到的是参数校验
  而不是漂移检测。
- 首版把"pin 根目录非空"当成全局脏机判定，一个场景的泄漏会污染其后所有场景（run1 误判 12 条）；
  改为逐场景基线 + 末尾按 `state.json` 反查泄漏归属。
- S14 曾在场景内部 umount tmpfs：守护进程还开着 socket/lock/journal，`umount` 必然 EBUSY，
  导致全绿的 run5 仍以 rc=1 收尾；改由 EXIT trap 在回收进程后卸载。
- S17 第一版断言"第二步失败应回 `REJECTED`"是错的：失败发生在**已经挂载之后**，契约是
  `ROLLED_BACK` + `error.code=APPLY_FAILED`（补偿成功、generation 不推进）；run7 因此失败，
  run8 按真实契约断言。
- run3 被首屏守卫 SKIP；run6 归档时"先解包再改名"的顺序写反，覆盖了本地 run4 的树
  （远端完好，已重新取回）。判定只取 run8。

## 5. 遗留项（不再是 P1 阻塞，按性质分类）

1. **已知限制（需产品决策）**：越权修改 class 条目不被纠正。要么给刷新加"抽样再断言"，
   要么把这条写进交付说明并排除相关声明。
2. **未跑的真机构造**：删除并重建同名 cgroup 路径（绑定失效后应拒绝继续而不是写错对象）。
   unit 已覆盖，真机层仍缺。
3. **未解释观测**：`/sys/fs/bpf/eulerpilot-v2` 一度整体消失（随后由 loader `mkdir_p` 重建）；
   `pin_dir()` 不可能返回裸根、loader 只 mkdir 不 rmdir，原因未查。
4. **重跑义务**：§3 的四项改动都在旗舰矩阵构建之后；正式结论需要在冻结构建上重跑
   （协议 §6.2）。这不影响 P1 的故障恢复判定，但影响 P3 的数字。

## 6. 阶段位置

P1 准出成立，下一阶段是 P2 的收口而非新增能力：P2 的功能件（FeatureWindow、规则路由、
SLO 门控、服务观测契约、切换抑制、自动 apply）已实现并单独留证，但"门控到底带来什么收益"
仍未测出，且部分判定受测量分辨率限制（p99 类约束不足以驱动自动判定）。P4 的 cgroup Skill 与
eBPF 事件链未开始。

## 7. 判定变更（同日两次）

- 第一次（run4 后）：**P1 不关闭**，阻塞项为 S14 存储故障分支 + 第二步失败缺真机证据。
  原文保留在 `evidence/p1/fail-safe-20261024.txt` §1/§2（run4 树）。
- 第二次（run8 后）：S14 修复并真机重验、S17 补上第二步失败的构造，**判定改为达成**，
  遗留项降为 §5 的非阻塞清单。回归：unit 443 条断言、`ctest` 6/6、验收 r26 六轮 rc=0，
  与 run8 同一份二进制（`ep-executord db04f38513f5…`）。
