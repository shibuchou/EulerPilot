# EulerPilot v2 技术报告

**选题**:proj2《面向 openEuler 的自适应资源管控 Agent》(功能挑战赛道)
**队伍**:西安邮电大学 · 三驱兄弟 · 队伍 ID T2026116649912743
**验收系统**:openEuler 24.03 LTS-SP3(内核 `6.6.0-schedpilot-sp3`,sched_ext 可用)
**仓库**:本 GitLab 仓库;文档索引见 [README.md](../README.md),运维手册见
[MANUAL_SP3.md](MANUAL_SP3.md),逐项验收判定见 [ACCEPTANCE_20261025.md](ACCEPTANCE_20261025.md)。

> 业绩声明纪律:本文出现的每一个数字都有仓库内证据文件支撑(文件名随行标注);
> 未实测的一律不写百分比;做不到的明确写 UNSUPPORTED 并给理由。

## 1. 问题与方案一句话

**用户态 Agent 感知负载并决策,特权执行服务是唯一写者,v2 自建 sched_ext 数据面执行;
一切决策被声明式契约约束,失败时向"无策略"方向回退。**

赛题要求"按负载特征动态选择或生成资源策略,重点通过 sched_ext 改善 CPU 调度,
扩展可用 eBPF 连接 cgroup 等域"。我们交付的是完整的闭环系统,不是一个调度器补丁:
感知(两条观测线)→ 决策(规则路由 + SLO/事件双重门控)→ 动作(两个效果域、
事务化发布)→ 恢复(故障矩阵 17 条目全过)→ 证据(预注册实验 + 可复现构建)。

## 2. 架构

```
┌─ 决策层(默认 advisory)──────────────────────────────┐
│ FeatureWindow(CPU/load/PSI) → 规则路由 → SLO 门控 → AutoPilot │
│ 服务观测(service.observe) 事件观测(event.observe)——只能更严 │
└───────────────┬───────────────────────────────┘
                │ 声明式策略(JSON Schema + 语义校验)
┌───────────────▼───────────────────────────────┐
│ ep-executord 唯一特权写者:flock 单实例、lease/fence、  │
│ generation、append-only WAL、事务状态机、幂等、崩溃 reconcile │
└───────┬───────────────────────────────┬───────┘
   sched 域                       cgroup 域
 v2 自建 loader(ops `eulerpilot`)  cpu.weight(写后读回+marker 恢复)
 link FD 经 SCM_RIGHTS 交执行服务    EventArm 武装的事件守卫
└───────┴───────────────────────────────┴───────┘
                内核:sched_ext map / cgroup v2 / raw tracepoint
```

两个效果域共用同一套事务骨架;策略 kind 与实例的域不匹配直接拒绝且零副作用。
完整契约:`design/schemas/*.json`(6 正例/27 反例由 `scripts/validate_design.py` 强制)。

## 3. 关键机制(创新点,各配证据)

**① 约束先于优化的发布链。** 生成策略限于声明式白名单;非人工来源的 apply 先过
门控(服务 SLO 半区 + 事件证据半区);判定顺序"权限→证据→抑制"本身是契约;
比较类判定先声明分辨率——策略敏感度不大于实测窗间噪声时判 `not run` 而非通过
(协议 §5.1,SP3 实测 p99 窗间 CV 3.8–20% 逼出来的护栏)。

**② 身份失效与可撤销授权。** 目标绑定持 pidfd/cgroup FD,重建/迁出即失效;
R5 事件链的 arm 捕获 watch 路径的 kernfs inode,同路径 cgroup 删除重建会使 arm
失效(WATCH_REBUILT)而不是把权限悄悄转给新对象——真机 Phase F 验证
(`evidence/p4/events-link-20261025.txt`)。

**③ R5 完整链路:hook→决策→动作→恢复。** raw tracepoint BPF 采集每 cgroup 的
runnable 等待(12.1k 事件/秒、零丢弃,泵自身 0.5% 单核)→ 只读泵 `ep-evtmon` →
`event.observe` → 武装守卫经标准事务路径调整 `cpu.weight` → 条件清除/TTL/泵死亡
三条路自动补偿。杀掉泵 = 观测断供 = 门冻结(六相真机时间线,`integration_events`)。

**④ 收益归因链。** 用消融回答"收益从哪来":A1(纯观测栈)−0.5% → **B(BASIC
数据面+观测)+1.75%** → C(CLASS+观测)+1.83% → 三条独立证据线(B 臂、
class-routing 消融零、mixed-foreground Dnc=D)互证:class 声明是**可审计的契约,
不是性能开关**;旗舰量级收益来自数据面切片/预算形态。参考实现 schedpilotd(E 臂)
同量级(+1.92%),新旧控制面一致性成立。

## 4. 验证体系(全部真机 SP3)

| 层 | 内容 | 结果 |
|---|---|---|
| 单元 | 587 断言(含事件链判定真值表、门控矩阵、崩溃恢复) | 全过 |
| 集成 | 8 个 CTest 必测项(真内核挂载、cgroup 域、事件链六相) | 8/8 |
| 验收 | 一键验收 r28–r30 各六轮(强制无 SKIP + 哈希 + 收尾断言) | 18/18 轮 rc=0 |
| 故障恢复 | fail-safe 矩阵 17 条目(崩溃/租约/TTL/非法策略/generation 冲突/双实例/外部改值/盘满/热插拔/两步写失败…) | run9–run11 均 17/17 |
| 漂移 | cfg 逐周期再断言 + class 轮换抽样纠正(越权改/删均被纠正) | 真机两场景过 |
| 包校验 | 二进制哈希 + 证据文件 + 契约三查 | 全过 |

## 5. 性能结论(预注册协议,块内配对 + 10000 次 bootstrap)

主场景:noisy-cpu redis 60k rps 开环、SLO 5ms、20 block(种子/参数/无效规则
预注册于 `experiments/flagship_params.json`;原始逐请求件留 SP3,哈希入库)。

| 对照 | goodput@5ms | 胜/负 | p99 |
|---|---|---|---|
| **C(固定 expert)vs A0(默认 fair)** | **GET +1.83% [1.38,2.26] / SET +1.39%** | **20/0、18/2** | **−39.6% / −32.8%** |
| C vs A2(静态 cgroup 倾斜) | +1.90%(20/0) | — | 收益非静态倾斜可解释 |
| D(自适应)vs A0 | +1.74% / +1.05% | 19/1 | −37.9% |
| A1(纯观测栈)vs A0 | −0.55~−0.82%(观测栈成本,如实披露) | — | +5~14% |
| B(BASIC+观测)vs A0 | +1.75% [1.45,2.09] | 10/0 | −40.8% |
| E(参考 schedpilotd)vs A0 | GET +1.92% [0.94,2.17];SET 打平 | 10/0 | −49.4% |
| 后台(BG 相对进度) | 0.998–1.006(前台收益未饿死后台) | — | — |

**必须同报的敏感性**:收益集中在 5ms 以上远尾,@2ms 阈值处 C/D 无优势甚至略负;
D vs C 在重跑构建上统计打平(CI 跨 0)。范围限定:单机单负载单 SLO 设定;
跨机 UNSUPPORTED。两代跨日方差(A1 的 GET、D vs C)两轮并列,不挑好看的那轮。
证据:`evidence/p2/flagship-{matrix,rerun}-*.txt`、`barms/earms/mixed-fg-noclass-20261025.txt`。

## 6. 复现

1. 取交付镜像(虚拟 80GiB/压缩 14.6GiB,SHA256 见 `PROVENANCE.md` 末节,含
   domain.xml/ks.cfg)或任意 SP3 + 自编译 sched_ext 内核;
2. 按 `MANUAL_SP3.md` §1 构建配方(含三个实测陷阱的规避);
3. `ctest` → 验收六轮 → fail-safe 矩阵 → `scripts/verify_package_sp3.sh` 全过;
4. 演示:`scripts/demo_sp3.sh`(八步时间线,连续三次彩排记录在
   `evidence/p5/demo-rehearsals-20261025.txt`)。

## 7. 边界与未做(如实)

- UNSUPPORTED(有实证理由):跨机验证(唯一验收级主机是 SP3);X 臂上游 scx
  对照(上游主线全 Rust 化且目标内核>6.6,验收机无法公平构建);adaptive 模式/
  心跳/PMU(设计上未实现,拒绝即为 fail-safe 形式)。
- 已知边界:性能为单机单负载单 SLO;BG 是"未受损"非"被优化";事件链 watch 按
  path+inode 匹配;演示视频链接与百度网盘地址由 README 提供。

## 8. 文件索引

| 想看什么 | 去哪 |
|---|---|
| 完整设计 | `docs/v2_redesign_2026.md`、契约 `design/` |
| 逐条验收(R1–R10) | `docs/ACCEPTANCE_20261025.md` |
| 运行/构建/演示手册 | `docs/MANUAL_SP3.md` |
| 接手说明(含工程约定) | `docs/HANDOVER_20261024.md` |
| 性能证据 | `evidence/p2/*.txt`(每个文件含完整性章节与边界) |
| 故障恢复证据 | `evidence/p1/fail-safe-20261024.txt`、`fail-safe-20261025-gen3/` |
| R5 两半 | `evidence/p4/{cgroup-domain,events-link,pump-overhead}-*.txt` |
| 二进制↔实验对应 | `PROVENANCE.md`(逐轮哈希) |

(视频说明与演示:见 README"演示"一节;10–30 分钟演示视频链接:〈百度网盘,
录制后填写〉)
