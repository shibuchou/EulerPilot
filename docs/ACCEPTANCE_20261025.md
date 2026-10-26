# 最终验收报告(R1–R10 对照)

日期:2026-10-25。判定环境:SP3 `6.6.0-schedpilot-sp3`、openEuler 24.03 LTS-SP3
(赛题指定的验收系统)。本文把项目状态映射到 `docs/CONTEST_REQUIREMENTS.md` 的
R1–R10;每条给状态、证据与不覆盖的边界。**不含任何未实测的百分比。**

## 总览

| 要求 | 状态 | 一句话 |
|---|---|---|
| R1 可扩展 Agent/Tool/Skill | **达成** | 版本化契约 + 两个效果域 + 第 8 个必测集成项 |
| R2 workload 感知与策略调整 | **达成** | FeatureWindow/路由/门控/服务与事件两条观测线,真机闭环 |
| R3 sched_ext 真实闭环 | **达成** | 唯一写者 + FD 传递 + 内核自证挂载;fail-safe 17/17 |
| R4 性能提升及复现 | **达成(带边界)** | 冻结构建复现 C vs A0 +1.83% [1.38,2.26](20/0) |
| R5 eBPF 扩展链路 | **达成** | hook→决策→动作→恢复 真机完整时间线(2026-10-25 补齐) |
| R6 创新性 | **达成(由消融支撑)** | 收益归因链闭合;门控/补偿/身份失效均量化 |
| R7 功能完整性 | **达成** | 见 R1/R2/R3/R5 与负路径矩阵 |
| R8 性能提升 | **达成(带边界)** | 同 R4;阈值敏感性同报 |
| R9 代码质量 | **达成** | 572 单测、ctest 8/8、GPL-2.0/NOTICE、PROVENANCE 逐轮哈希 |
| R10 演示效果 | **达成** | 八步演示连续三次彩排全过,收尾干净 |

## 逐条依据

**R1**:`design/schemas/*.json` 五个版本化契约 + `EventArm`(jsonschema 6 正例/27
反例);Skill 生命周期 probe→plan→snapshot→apply→verify→rollback→reconcile;
效果域 `sched` 与 `cgroup`,域不匹配零副作用拒绝(`UNSUPPORTED`)。

**R2**:FeatureWindow(CPU/load/PSI,带 valid/age/missing)→ 规则路由 → SLO 门控
(服务半区+事件半区,只能更严)→ AutoPilot(默认关闭,租约显式交接才动作)。
服务观测与事件观测两条真机闭环,断供可报告(`observer-gap`、事件 STALE)。

**R3**:v2 自建 loader(ops `eulerpilot`,每实例 pin)attach 后经 SCM_RIGHTS 交
link FD;apply 内核自证(state/ops/enable_seq 等值判定,发布窗口按内核真实顺序
处理);`enable_seq` 发布窗口、runnable-stall(亲和性改投)两个内核级根因修复
留档。fail-safe 矩阵 17 条目(run8 与冻结构建复跑均 17/17)。

**R4/R8**:旗舰矩阵(20 block × 5 臂,seed 预注册,块内配对 + 10000 次 bootstrap)
在**交付级冻结构建上复现**:C vs A0 goodput@5ms **GET +1.83% [1.38,2.26](20 胜
0 负)、SET +1.39%、p99 −39.6%**;D vs A0 +1.74%;C vs A2 +1.90%(非静态倾斜);
A1 纯观测栈 −0.55~−0.82%;BG 相对进度 0.998–1.006(未受损)。**边界**:单机单负载
单 SLO;@2ms 阈值处无优势(敏感性同报);跨机 UNSUPPORTED(SP4 cgroup v1/无 PSI)。

**R5**:两半闭合——动作端 cgroup Skill(cpu.weight 写读回、漂移再断言、崩溃
marker 恢复、抽样纠正越权改写)+ 感知端事件链(raw_tp BPF → 只读泵 ep-evtmon →
event.observe → 武装守卫 → 事务路径升权/补偿)。真机六相时间线:空载不动、饥饿
升权、撤载补偿、杀泵门冻结+TTL 兜底、**重建同路径 cgroup 使 arm 失效(WATCH_REBUILT,
不转授)**、泵单独零写入。

**R6**:三条独立证据线互证"class 是契约不是性能开关"(B 臂 +1.75% 先于 C 的
+1.83%;class-routing 消融零;mixed-foreground Dnc=D=2/3 联合 SLO);参考实现
E 臂同量级(GET +1.92%),新旧控制面一致性;切换空窗 303–665ms、门控"延迟器"
行为、40s 延迟代价上界均量化。归属:专家路由/门控思想按 PROVENANCE 归属上游,
自证贡献在约束链与可撤销发布。

**R7**:负路径全覆盖(无效策略 4 类拒绝、generation 冲突、双实例、外部改值、
成员迁出、盘满、热插拔、两步写入失败),UNSUPPORTED/SKIP 从不用 PASS 遮盖
(adaptive/心跳/PMU 三条明记 UNSUPPORTED by design;X 臂 UNSUPPORTED:上游
scx 主线已 Rust 化且目标内核高于 6.6,验收机上无法公平构建)。

**R9**:单元 572 断言、ctest 8 必测项、验收 r28(六轮)与 r29(冻结构建)、
包校验脚本(哈希+证据文件+契约三查);GPL-2.0 + NOTICE + PROVENANCE 逐轮哈希;
文档与代码同步更新,负结果与失败现场全部保留。

**R10**:`scripts/demo_sp3.sh` 八步时间线(契约→基线→生效→扰动实时应答→崩溃
恢复→cgroup 域→事件链→干净收尾),**最终形态连续三次彩排 PASS**,每次收尾
`state=disabled`、`pins=0`;演示失败也保留现场与 FAIL 行(方法论本身可展示)。

## 两代冻结构建的声明(包校验按此解读)

- **gen-1** `ep-executord 3693c9ac…`:性能矩阵(旗舰重跑/B/E)的二进制,性能
  结论全部指向它;
- **gen-2** `ep-executord 35e80b99…`:watch 身份加固(仅 `--event-arm` 路径),
  回归 = r29 六轮 + fail-safe run10 17/17;
- **gen-3** `ep-executord 6f667b2e…`(最终交付):再加 cgroup 域双写保护(skill
  层,与 sched 域预检对称),回归 = ctest 8/8 + r30 六轮 + fail-safe run11 17/17。
- gen-2/gen-3 的改动均不在矩阵臂执行路径上;性能主张以 gen-1 溯源。
- **交付镜像**已制作:`schedpilot-sp3-20261025.qcow2`(虚拟 80GiB/压缩 14.6GiB,
  SHA256 `d9dd5030…`,干净关机后转换;含 gen-3 源树与冻结二进制),与
  domain.xml/ks.cfg/README/SHA256SUMS 同在跳板机 `/home/ubuntu/sp3-vm/deliverable-20261025/`。

## 跨版本对照(EulerPilot v1 → v2)

评审可能直接问"比你们上一版快多少"。这一节把能说的与不能说分开。

**判定:跨版本性能差值 = UNSUPPORTED(未做同机配对测量)。** 三条理由:①赛题 R4/R8
要求的基线是"默认 fair 对照 + 强基线 + quiet 回归",不是上一版本;②`docs/EXPERIMENT_PROTOCOL.md`
要求所有臂在同一台机器同一内核启动下配对,跨内核差异不得混入配对表;③v1 的 scx 路径
**未在 SP3 上重建验证过**,而其自带配方存在两处已知障碍:`scripts/build_scx_eulerpilot.sh`
要求 `KERNEL_SRC/tools/sched_ext` 存在否则退出(v1 自己在 `sched/README.md:13` 记录 SP3
官方内核不含可直接启用的 sched_ext),其 `Makefile` 又经 pkg-config 链系统 libbpf——SP3 的
系统 libbpf 1.2.2 在 `SCX_OPS_LOAD` 处段错误是 v2 实测过的(v2 因此改用内核树静态 libbpf
≥1.3),v1 的 loader 是否命中同一问题属**推断,未实测**。

**可辩护的是设计替换论证。** v1 自报的正式实验(环境:openEuler 24.03-LTS **SP4** +
自建 `eulerpilot-scx` 内核;负载:**闭环** `redis-benchmark`;统计:paired per-run t 区间;
且其 P99 符号约定与 v2 相反——正=更好。见 `EulerPilot/reports/v6_formal_experiment_suite_20260726.md`,
绑定 `7a99d870`)与 v2 在 SP3 上的开环结论并列如下:

| 项目 | v1 自报(SP4,闭环 RPS) | v2 实测(SP3,开环 goodput@5ms) |
|---|---|---|
| scx 正常路径 vs 默认 | GET **+3.40% [−3.28, +10.08]**(区间跨 0,即其自身实验未获得统计支持的收益) | C vs A0 **+1.83% [1.38, 2.26],20/0** |
| scx always-active 模式 | **−62.62% [−81.57, −43.67]**,P99 −220% | adaptive 模式在 apply 阶段直接拒绝(不留下未验证的自动化通道) |
| 静态 cgroup 倾斜 | +68.30% [50.74, 85.86](闭环,延迟改善几乎 1:1 转成吞吐) | A2 为受控臂之一;C vs A2 **+1.90% [1.63, 2.32]** |
| quiet 回归 | Redis 36005.720 → 26687.541 RPS(**−25.88%**) | quiet 场景纳入门控判据;A1 纯观测栈成本 −0.55%~−0.82% 如实披露 |
| 收益归因 | 未做消融 | A1→B→C 增量链 + class 路由消融为零 + Dnc=D |

**两张表不能横向比大小**:闭环 RPS 与开环 goodput@SLO 是不同估计量(开环下吞吐上限被
offered 钉死,收益只能体现在"满足 SLO 的比例"与尾延迟上),机器、内核、符号约定亦不同。
因此本项目的表述是"v1 的 scx 路径在它自己的实验里没有拿到统计支持的收益并在两个模式下
显著为负;v2 替换数据面后在验收系统上拿到 20/0 的正收益",**不给 v1↔v2 的百分比差值**。

**能力维度的差异是可举证的**(依据 `docs/audits/eulerpilot_audit.md`,该审计明确
"未运行旧项目、未加载 BPF",且机制分析对象 HEAD `15e5a21d` 与其历史测量绑定的 `7a99d870`
不是同一提交):

| 能力 | v1 | v2 |
|---|---|---|
| 特权写者 | CLI 与 Agent 均可直接写 gate/class map,无基于 generation 的原子比较更新(审计第 55 行) | 唯一执行服务 + flock + 租约/栅栏 + 世代号;双实例与越权写被矩阵拒绝 |
| 崩溃恢复 | 无写前日志与 reconcile 语义 | append-only WAL + 启动孤儿回收 + `shutdown()` 补偿退出;fail-safe 17/17 |
| 效果时限 | 无 TTL 兜底 | `policy_ttl_ms` 硬上限,到期自动回退 |
| 状态自证 | 部分读回 | sched 三项联合判定 + class 撤销读回 + cgroup 写后读回 + 漂移再断言 |
| 契约 | 无版本化 schema | 5 个 schema + EventArm,6 正例 / 27 反例强制 |
| 扩展域 | cgroup 写入 + 自建 scx | 两个效果域共用事务骨架 + eBPF 事件链(hook→决策→动作→恢复) |
| 溯源 | 结果目录在仓库外 | 逐轮二进制哈希、失败轮现场、交付镜像 |

## 未完成与已知边界(不可省略)

- cross-machine UNSUPPORTED;X 臂 UNSUPPORTED(理由如上);adaptive/心跳/PMU
  UNSUPPORTED by design。
- 泵/BPF 开销已量化(`pump-overhead-20261025.txt`);cgroup 双写保护已闭合
  (gen-3,skill 层拒绝 + 587 断言)。
- 泵/BPF 开销已量化(0.5% 单核,goodput 配对差 −0.010%,`pump-overhead-20261025.txt`);map 饱和(>4096 cgroup)未构造。
- cgroup 域无"已有效应再 apply"的通用保护(守卫自查 active_,人工并发靠操作纪律)。
- SP3 镜像已完成(qcow2,见"两代冻结构建的声明"末条);镜像的**搬运/提交方式**待比赛对交付物形态的要求确认(14.6GiB)。
