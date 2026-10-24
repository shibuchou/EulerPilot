# 来源、许可与迁移边界

记录日期：2026-10-21。本项目是面向新赛题的独立工作目录，本轮没有向远端同步，也没有修改两个旧仓库。

## 输入版本与快照

| 输入 | 本轮核验版本 | 用途 |
|---|---|---|
| `D:\code\Ubuntu\EulerPilot` | HEAD `15e5a21d9ec9d53a94b4aa35812b33dae67ac6a3`，存在未提交设计文档 | Agent/Skill/恢复思路、数据面缺陷和历史报告 |
| `D:\code\Ubuntu\schedpilot` | HEAD `0aeca0cba2b4c9b4d4292847d71018981a04b552`，存在未跟踪PPT旁路材料 | 数据面、分类器、实验与SP3历史路径 |
| 原设计稿 | SHA256 `f82b469ac9648623be060b0fbf05ccf97f08e46156fab44fd30ce2017a9ae14f` | 本次v0.2的直接输入 |
| “开源创新”会话 | thread `019e8c80-8d58-7ea1-b67d-74a33a90097c` | 用户授权读取的相关封口/提交结论，未导出完整会话 |

原设计稿原样保存在 [v0.1快照](docs/reference/v2_redesign_2026.v0.1.md)，本轮已比较文件SHA256。13个关键输入的字节数、时间戳、哈希及两个仓库状态见 [source-inputs.json](evidence/design/source-inputs.json)。它是输入清单，不是全部代码/旧原始归档的验证证明。

会话显示旧项目曾完成提交与release封口；本轮未核查远端release当前状态。旧正式实验的tested commit与当前HEAD不同，详见审计；禁止以当前HEAD自动代表当时测试代码。

## 本次迁移范围

- 已迁入：原设计稿只读快照、来源清单；SchedPilot `0aeca0c` 的 sched_ext 数据面源与 loader
  （`sched/`，GPL-2.0，逐文件哈希与修改见 `sched/README.md`）。其余为新写的方案、审计和工具。
- 尚未迁入：旧 EulerPilot C/C++、SchedPilot daemon/分类器、旧前端、完整历史数据和发布包。
- 旧结果只作有明确出处的历史证据，不构成v2性能成绩。
- 后续逐模块记录：源仓库/commit、源路径与SHA256、目标路径、SPDX/版权、修改说明、依赖和新验证结果。不能简单改名后宣称全部原创。

## P0 运行证据（2026-10-21 追加）

- SchedPilot `0aeca0c` 以 git bundle 传入 SP3，克隆到 `/root/p0/schedpilot-0aeca0c`；
  bundle sha256 `88e79a93e5c89ee25d66f7b51be8fabb2e734d4eeffba487990add34fef8c885`。
- 参考构建使用 SP3 源码树 `kernel-source-6.6.0-145.3.34.165.oe2403sp3`；注入文件备份在
  `/root/p0/backup-injected`，旧 `/root/schedpilot` 未覆盖。产物哈希、构建命令与实验 ID 见
  `evidence/p0/sp3-reference-manifest.json`。
- SP3 启动参数加入 `psi=1 systemd.unified_cgroup_hierarchy=1`。这是环境改动，不是源码改动；
  回滚命令见 `docs/p0/SP3_ENV_REPORT.md`。
- A/B/C 是 3 轮 × 15s 的 smoke，原始数据在 VM `/root/p0/results/`，紧凑证据与归档哈希在
  `evidence/p0/abc-smoke-20261021/`。它们不构成性能结论。

## P1 新增依赖与环境改动（2026-10-21 追加）

- `third_party/nlohmann/json.hpp` 3.11.3（MIT），SHA256
  `9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6`；仅用于 JSON 解析/序列化，
  未修改源码，许可文本见同目录 `LICENSE.MIT`。
- v2 控制面使用 C++20 + CMake ≥3.20；无其他第三方库。
- SP3 目标机安装 `cmake 3.27.9` 与 `perf 6.6.0-145.3.35.166`。这是环境改动，不是源码改动；
  它解除了 P0 的 perf 缺口，但不改变 P0 smoke 当时没有 perf 计数的事实。

## P1 v2 自建 loader（2026-10-21 追加）

- `sched/` 是从 SchedPilot `0aeca0c` 迁移的 **GPL-2.0** sched_ext 数据面组件：
  - `scx_schedpilot.bpf.c` 复制后仅改 struct_ops `.name` 为 `eulerpilot`
    （原 sha256 `3d057e6f…`，当前 `119e1964…`）；`intf.h` 原样复制（`c72e7eec…`）；
  - `loader.c` 由 SchedPilot loader 修改而来（v2 默认 pin 路径、新增 `--pin-dir`、
    移动 pidfile、通用 mkdir_p），原文件 sha256 `0fe906c7…`，当前 `1acdd6bd…`；
  - `scx_schedpilot.bpf.skel.h` 由重建后的 BPF 对象生成（`67e7228f…`）；
  - `sched/schedctl.cpp` 为新增 GPL-2.0 helper（包含 `intf.h`），负责 cfg map 参数写入、
    回读与恢复；执行服务只调用它，不长期持有 map FD；
  - `scx/*.h` 来自 SP3 内核树 `tools/sched_ext/include/scx/`。
  逐文件哈希与修改清单见 `sched/README.md`，GPL-2.0 全文见 `sched/LICENSE.GPL-2.0`。
- v2 构建目标 `ep-sched-loader` 链接内核树静态 libbpf 1.3（sha256 见 `evidence/p1/sp3-v2-loader.txt`）。
  SP3 系统 libbpf 1.2.2 在加载该 struct_ops 对象时 SIGSEGV；这是已验证的部署约束，
  记录在 `sched/README.md`。
- pin 命名空间为每实例 `/sys/fs/bpf/eulerpilot-v2/<instance_id>`，struct_ops 注册为
  `eulerpilot`；`sched` 模式 loader 一次性 attach 后把 link FD 交给执行服务并退出，
  `sched-ref` 保留子进程对照。
- 真实测试证据：`evidence/p1/sp3-sched-integration.txt`、`evidence/p1/sp3-v2-loader.txt`、
  `evidence/p1/sp3-instance-pin.txt`、`evidence/p1/sp3-cfg-params.txt`、
  `evidence/p1/sp3-fd-link.txt`、`evidence/p1/sp3-cgroup-id.txt`、
  `evidence/p1/sp3-pid-target.txt`、`evidence/p1/sp3-pidfd-held.txt`、
  `evidence/p1/sp3-lease-expiry.txt`、`evidence/p1/sp3-policy-ttl.txt`、
  `evidence/p1/sp3-deadline.txt`、`evidence/p1/abc-nostall-20261021.txt`、
  `evidence/p2/feature-window-host.txt`、`evidence/p2/rule-router.txt`、
  `evidence/p2/slo-gate.txt`、`evidence/p2/slo-veto.txt`、
  `evidence/p2/classmap-bg.txt`、`evidence/p2/classmap-bg-cgroup.txt`、
  `evidence/acceptance-sp3-20261021.txt`、`evidence/acceptance-sp3-strict-20261022.txt`、`evidence/acceptance-sp3-20261022/`、`evidence/acceptance-sp3-strict-20261022-r3.txt`、`evidence/acceptance-sp3-20261022-r3/`、`evidence/p1/helper-budget-20261022.txt`、`evidence/acceptance-sp3-strict-20261022-r4.txt`、`evidence/acceptance-sp3-20261022-r4/`、`evidence/p1/wal-hardening-20261022.txt`、`evidence/acceptance-sp3-strict-20261022-r5.txt`、`evidence/acceptance-sp3-20261022-r5/`、`evidence/p1/active-bindings-20261022.txt`、`evidence/acceptance-sp3-strict-20261022-r6.txt`、`evidence/acceptance-sp3-20261022-r6/`、`evidence/p1/fairness-pilot-20261022.txt`、`evidence/p1/fairness-sp3-20261022.json`、`evidence/p1/fairness-sp3-20261022.stats.txt`、`evidence/p1/fairness-budget-20261022.txt`、`evidence/p1/fairness-diag2-20261022.json*`、`evidence/p1/fair-arm{1,2,3,4-quota,5-realtime}.json*`、`evidence/p1/fair-stab{1,2,3}.json*`、`evidence/acceptance-sp3-strict-20261022-r7a-fail.txt`、`evidence/acceptance-sp3-strict-20261022-r7b-fail.txt`、`evidence/acceptance-sp3-strict-20261022-r7c-pass.txt`、`evidence/acceptance-sp3-strict-20261022-r7c-seqtrace.log`、`evidence/acceptance-sp3-strict-20261022-r7loop-1..4.txt`、`evidence/acceptance-sp3-20261022-r7c/`、`evidence/acceptance-sp3-strict-20261022-r8-round1..6.txt`、`evidence/acceptance-sp3-20261022-r8/`、`evidence/p1/enable-seq-verify-20261022.txt`。
- `--skill sched` 采用 FD 传递：loader 一次性 attach 后把 struct_ops link FD 通过
  SCM_RIGHTS 交给执行服务并退出；执行服务崩溃时 FD 关闭即 detach。`sched-ref` 保留
  子进程模式用于对照与测试。

## 数据面公平性修正（2026-10-22 追加）

- `sched/scx_schedpilot.bpf.c` 本轮 `f568b0e8…` → `679f7836…`：`sp_task_ctx` 增 `run_ns`，
  `stopping()` 的 `consumed` 改用真实墙钟时长（超过 `SP_MAX_RUN_DELAY_NS` 回退 slice 差值）。
  改动全部落在自有 GPL-2.0 组件内，未引入新来源或新第三方依赖。
- `sched/scx_schedpilot.bpf.skel.h` `4617297c…` → `e76d7af4…`，在 SP3 重新生成：
  `clang 17.0.6` 编译 `.bpf.o`，`/root/ep-sched-build/build/sbin/bpftool` v7.3.0（libbpf 1.3）
  `gen skeleton`，`vmlinux.h` 取 `/root/ep-sched-build/build/include`。
- 产物 `ep-sched-loader` `7e896f40…` → `d29aa3bb…`；`ep`、`ep-executord`、`ep-schedctl` 未变。
- `tests/fairness_sp3.sh` `2988ec99…` → `076f35d9…`：跨 CPU 求和、运行中采样 `fair_budget`
  （该 map 未被 pin，按 map 名读取）、片长配额与 `throttle > 0` 断言。
- 远端同步走暂存目录：先确认 `/root/epv2-src` 内这些文件与本地 HEAD 哈希一致（无未同步改动），
  再复制到 `/root/epv2-staging-20261022/` 校验哈希与 `bash -n`，通过后 `install` 生效；
  原 skeleton 备份为 `/root/epv2-staging-20261022/scx_schedpilot.bpf.skel.h.orig`。
- 严格验收本轮出现 2 次失败与 1 次通过（`integration_sched` 的
  `enable_seq changed since attach`），失败日志与 `enable_seq` 采样一并归档，未只保留通过样本。
  与本轮 BPF 改动的因果关系未确认。

## 持续在位性检查（2026-10-22 追加）

- 新增 `Skill::liveness`（`include/ep/skill.hpp`）、`SchedSkill::presence_ok()/liveness()`
  （`include/ep/sched_skill.hpp`、`src/skills/sched_skill.cpp`）、监控接入与 3 次计数
  （`include/ep/executor.hpp`、`src/executor/executor.cpp`）、`test_sched_liveness_takeover`
  （`tests/unit/test_main.cpp`）。全部为 v2 自有代码，无新第三方来源。
- 文件哈希（HEAD → 工作区，本地与 SP3 `/root/epv2-src` 已逐字节核对一致）：

  | 文件 | HEAD | 工作区 |
  |---|---|---|
  | `include/ep/skill.hpp` | `1c6909a7…` | `9c07a014…` |
  | `include/ep/sched_skill.hpp` | `64bad8f4…` | `cc2d6bcf…` |
  | `include/ep/executor.hpp` | `a8cbb348…` | `8c12246f…` |
  | `src/skills/sched_skill.cpp` | `dcce4719…` | `df35121b…` |
  | `src/executor/executor.cpp` | `52210404…` | `7baa5d96…` |
  | `tests/unit/test_main.cpp` | `f2a3ff66…` | `44edf306…` |

- 产物链：`ep-executord 04163c90… → fe4314d9…`（`enable_seq` 校验语义）`→ d2caa3a3…`（在位性检查）；
  `ep_unit_tests` 最终 `e414b484…`；`ep 6b33142a…`、`ep-sched-loader d29aa3bb…`、
  `ep-schedctl af759004…` 未变。
- 覆盖层级如实记录：在位性检测只有 **unit（假 sysfs）+ 真实回归**两层，
  SP3 上未做真实第三方顶替（需另一个 struct_ops 调度器真 attach 从而夺走调度器所有权，
  与"对照实现互斥运行"的边界冲突，待单独获批）；同名实例顶替与 `fd_passing` 分支尚未覆盖。
- 证据：`evidence/p1/liveness-watch-20261022.txt`、`evidence/p1/fair-liveness.json*`、
  `evidence/acceptance-sp3-strict-20261022-r9-round1..3.txt`、`evidence/acceptance-sp3-20261022-r9/`。

## enable_seq 发布窗口修正（2026-10-22 追加，取代同日较早的校验降级方案）

- 内核依据（读 SP3 源码，不是推测）：`kernel/sched/ext.c` 中 `scx_enable_seq`（:949）**唯一**递增点在 :5518，
  位于 `scx_enable()` 末尾，晚于状态置为 enabled、`pr_info` 与同步 `kobject_uevent`；
  CPU 热插拔使用独立的 `scx_hotplug_seq`（:942/:3433/:4364），不会动这个计数。
  `kernel/bpf/bpf_struct_ops.c:56` 的 `struct bpf_struct_ops_link` 只有 `link` + `map`，
  `.release` 为空且无 `.detach`，UAPI `bpf_link_info` 的 struct_ops 分支只有 `map_id` —
  因此 link FD 无法回答"我是否仍是当前生效的调度器"。
- 同日更早的 `evidence/p1/enable-seq-verify-20261022.txt` 把成因归为"第三方 bump / 热插拔"，
  并把判定降级为"只判回退"。该根因判断已被上述源码推翻，原文保留并在其第 6 节作出更正；
  本条才是最终方案，且顺带恢复了同名实例顶替的可检测性。
- 代码变更：新增 `SchedSkill::wait_enable_seq_stable()`；`presence_ok()` 并入 `enable_seq` 等值判定
  （`verify` 与 `liveness` 共用）；`apply()` 以 snapshot 记录的 pre-enable 计数器为基准等待发布后再记录，
  为此把原本匿名的 `Snapshot` 参数命名。
- 单测：`test_sched_enable_seq_semantics`、`test_sched_enable_seq_publish_window`（复刻内核发布顺序的
  延迟递增）、`test_sched_liveness_takeover`（state/ops 不变、仅计数器变化的同名顶替）；
  另外 4 个既有假 loader 场景统一改为初始化并按顺序递增 `enable_seq`，否则这条语义在单测层恒不可见。
- 文件哈希（HEAD → 工作区；本地与 SP3 `/root/epv2-src` 六个源文件逐字节核对一致）：
  `include/ep/sched_skill.hpp 64bad8f4… → 3e7ae837…`、
  `src/skills/sched_skill.cpp dcce4719… → 20b077c0…`、
  `tests/unit/test_main.cpp f2a3ff66… → c06d0bc1…`
  （`skill.hpp`、`executor.hpp`、`executor.cpp` 见上一节）。
- 产物：`ep-executord d2caa3a3… → 9510860e…`、`ep_unit_tests e414b484… → a3bdebf4…`；
  `ep 6b33142a…`、`ep-sched-loader d29aa3bb…`、`ep-schedctl af759004…` 未变。
- 证据：`evidence/p1/enable-seq-publish-20261022.txt`、
  `evidence/acceptance-sp3-strict-20261022-r10-round1..6.txt`、`evidence/acceptance-sp3-20261022-r10/`。

## 真实顶替实验与启动期原因贯通（2026-10-22 追加）

- 实验脚本与现场：`evidence/p1/takeover-sp3.sh`（可复现脚本，头部已写明未达成原因）、
  `evidence/p1/takeover-20261022.txt`、`evidence/p1/takeover-20261022/ep-take-{A,B}/`
  （两个 daemon.log、apply/status JSON、A 的 pins 快照）。实验未制造出顶替：
  第二个 v2 实例在 attach 之前就被 `recover_orphan()` 判冲突并拒绝 apply，
  这是唯一写者约束的体现；真实顶替因此只能借第三方 ops 名的调度器验证，尚未获准执行。
- 实验暴露的真实缺陷已修：`Executor::init()` 打印的是 `recover_orphan` 的出参而非返回的 `detail`，
  导致 `orphan recovery failed:` 后为空、apply 拒绝只剩笼统提示。新增 `startup_degraded_reason`
  贯通日志、`policy.status` 与 apply 的 `RECOVERY_REQUIRED` detail，
  并加 `test_startup_degraded_reason_surfaced`（假 sysfs，无需 root）。
- 文件哈希（工作区；本地与 SP3 `/root/epv2-src` 一致）：
    `include/ep/executor.hpp` → `b54802a938324a19…`
  `src/executor/executor.cpp` → `a37cadce183b9228…`
  `tests/unit/test_main.cpp` → `71a8c28b278c19f9…`
- 产物：`ep-executord 9510860e… → 60026843…`、`ep_unit_tests a3bdebf4… → f9423a09…`；
  `ep 6b33142a…`、`ep-sched-loader d29aa3bb…`、`ep-schedctl af759004…` 未变。
- 回归：`ctest` 6/6 0 跳过；`accept_sp3.sh` 2 轮 rc=0
  （`evidence/acceptance-sp3-strict-20261022-r11-round1..2.txt`、
  `evidence/acceptance-sp3-20261022-r11/`，其 SHA256SUMS 与上述产物一致）。

## 第三方顶替实测与定位下调（2026-10-22 追加）

- 实验脚本 `evidence/p1/takeover-thirdparty-sp3.sh`，结论
  `evidence/p1/takeover-thirdparty-20261022.txt`。
- 实测事实：参考实现 loader（`/root/p0/schedpilot-0aeca0c/build/scx_schedpilot`，
  sha256 前缀 `3da28c29…`）attach 报 `[SCX_BUG] scx_schedpilot.c:376 (Device or resource busy)`；
  v2 自己的第二个 loader（`d29aa3bb…`）同样以 EBUSY 失败于 `sched/loader.c:468`；
  两次尝试中 A 的 `state`、`root/ops`、`enable_seq` 均无变化，A 也未误报。
  结论是"执行服务存活期间被顶替"在本内核不可构造，顶替检测的证据层级停留在 unit。
- 因此 `evidence/p1/liveness-watch-20261022.txt` 与
  `evidence/p1/enable-seq-publish-20261022.txt` 各追加第 7 节（原文保留），
  把 `enable_seq` 等值判定从"同名顶替唯一信号"下调为顺序接管采样盲区的兜底；
  代码注释同步修正（`src/skills/sched_skill.cpp` 工作区 `f6b32dc4…`，无逻辑改动）。
- 参考实现资产为只读使用：其 pin 空间 `/sys/fs/bpf/schedpilot` 条目数前后一致、未被修改，
  本次实验未运行 `schedpilotd`（脚本内有显式拒绝检查）。
- 收尾失误与纠正：实验二脚本只清理了自建的 `--pin-dir`，遗留了执行服务 A 的 instance pin；
  确认 `sched_ext=disabled` 后删除，最终 `pins=0`、`/sys/fs/bpf/eulerpilot-v2` 不存在、
  无 executord/loader 进程。
- 产物（注释修正后重建）：`ep-executord 60026843… → 4681d9e1…`、
  `ep_unit_tests f9423a09… → 2db00afc…`；`ep`、`ep-sched-loader`、`ep-schedctl` 未变；
  `ctest` 6/6、0 跳过。

## 自动 apply 最小切片（2026-10-22 追加）

- 全部为 v2 自有代码，无新第三方来源。新增纯函数决策模块 `AutoPilot`
  （`include/ep/autopilot.hpp`、`src/core/autopilot.cpp`），在执行服务监控线程内调用既有的
  `policy.rollback`/`policy.apply` 路径，因此执行服务仍是唯一写者；自动通道无权改变
  `targets`/`safety`，且只有租约被显式交给它时才动作。
- CLI：`ep-executord` 增加 `--auto-apply/--auto-policy/--auto-owner/
  --auto-min-interval-ms/--auto-lease-ttl-ms`；`ep lease` 增加 `--owner`
  （原先租约身份硬编码为 `cli-<random>`，导致无法显式移交控制权）。
- 文件哈希：
  `include/ep/autopilot.hpp` — 工作区 `fc8d761082e62546…`
  `src/core/autopilot.cpp` — 工作区 `c1e7497e5f3c54de…`
  `include/ep/executor.hpp` — 工作区 `a18418e481c13ba4…`
  `src/executor/executor.cpp` — 工作区 `7e60c0ded63868a0…`
  `src/executor/main.cpp` — 工作区 `e02c825a429d5c83…`
  `src/cli/main.cpp` — 工作区 `fb0ef93e6fe6763a…`
  `tests/unit/test_main.cpp` — 工作区 `5359a69d0cf5d418…`
  `CMakeLists.txt` — 工作区 `16145fcba437d9c1…`
  `experiments/auto_apply_pilot_sp3.sh` — 工作区 `de7510092e981cd0…`
- 实验与证据：`experiments/auto_apply_pilot_sp3.sh`、
  `evidence/p2/auto-apply-pilot-20261022.txt`、`evidence/p2/auto-apply-pilot-20261022/`
  （人工 apply、交接、切换、收回四阶段 JSON 与 daemon 日志）。
- 回归：`ctest` 6/6 0 跳过、0 编译警告；`accept_sp3.sh` 2 轮 rc=0
  （`evidence/acceptance-sp3-20261022-r13/`、`acceptance-sp3-strict-20261022-r13-round1..2.txt`）。
  产物 `ep ce49fca9…`、`ep-executord 5fcdfb1e…`；数据面未改（loader/schedctl 不变）。

## 切换代价量化（2026-10-22 追加）

- 新增三个 v2 自有测量资产（不属于交付数据面）：`experiments/sched_switch_probe.c`（GPL-2.0 标识，
  纯观测：1ms 轮询 sched_ext 状态 + 绝对 deadline 周期唤醒）、`experiments/analyze_switch_downtime.py`、
  `experiments/auto_apply_downtime_sp3.sh`。未引入新第三方依赖。
- 实测：每次自动切换留下 665.0ms / 303.1ms 的无 v2 策略空窗；同负载对照下空窗内 1ms 周期唤醒
  p99 由 1.2–2.0ms 恶化到 13.2–16.1ms、max 由 2.2–3.0ms 到 20.2–23.1ms；撤载后的空窗无可测毛刺；
  `enable_seq` 移动 3 次与 3 次 attach 一一对应。
- 本轮自身两个脚本缺陷已修：`watch` 分支 argc 判断错误导致观察进程从未启动；主体无参数 `wait`
  会等待长期运行的执行服务而永久阻塞。两者均已修复，并做过一次完整端到端复跑
  （run2，`script_rc=0`，升载空窗 621.0ms 与首轮 665.0ms 相差 6.6%）。
- 证据：`evidence/p2/switch-downtime-20261022/`（`summary.json`、`watch.tsv`、`daemon.log`、
  `raw-SHA256SUMS`）与 `evidence/p2/switch-downtime-20261022-run2/`。原始探针二进制各 1.92MB，留在 SP3 `/root/epv2-evidence/auto-downtime/`，
  仓库只保存派生结果与哈希，避免塞入大文件。

## 自动 apply 切换抑制（2026-10-22 追加）

- 新增 v2 自有代码：`include/ep/autopilot.hpp` `c88af3e7279ec6e1…`、`src/core/autopilot.cpp`
  `efbc4e1cd6a46c57…`（纯函数决策，不写调度器）；改动 `src/executor/executor.cpp` `7facc892fb8880be…`、
  `include/ep/executor.hpp` `5d231260f4e0fea7…`、`src/executor/main.cpp` `b925d510d566349b…`
  （记账与 5 个 `--auto-dwell-ms/--auto-candidate-hold-ms/--auto-budget-window-ms/`
  `--auto-budget-max-switches/--auto-measured-downtime-ms` 参数）、
  `tests/unit/test_main.cpp` `015020890d062bb1…`（`test_autopilot_rules` 20 条断言）。
  `--auto-dwell-ms` 与 `--auto-candidate-hold-ms` 只接受 1000..3600000，超出即拒绝启动。
- 实验脚本：`experiments/auto_apply_suppression_sp3.sh`。第 7 节三次运行用 `78d28badf75785a2…`；
  修掉 awk 字符串比较（`gsub` 后与未初始化变量比较走字典序，`"8027" > "59753"`）并补 executor
  就绪 fail-fast 后为 `c4382360d6271baa…`，与 SP3 `/root/epv2-src` 逐字节一致。差异只在断言与失败处理，
  不改变被测产品行为。
- 实测（SP3，8 vCPU，`6.6.0-schedpilot-sp3`，逐秒时间线原始件已入库）：dwell 门挡到
  `in place 59755ms`/60000 才允许切换；候选保持门单独挡到 38161ms/40000（同轮 dwell 行数 0）；
  预算门用满 1/1 后在 100s 观察窗内 65 次拒绝回落。三张表的门行数普查见证据文件第 7 节。
- 证据：`evidence/p2/autopilot-suppression-20261022.txt`（第 6 节是对上半部分的更正，不是追加结论：
  断言计数 17→20、目录引用的运行对错、"candidate_hold 与预算只有单测证据"已被推翻）、
  `evidence/p2/auto-suppress-dwell-20261022/`、`auto-suppress-candidate-20261022/`、
  `auto-suppress-budget-20261022/`（各含 `timeline.csv`/`daemon.log`/`apply.json`/`handover.json`），
  失败尝试 `auto-suppress-candidate-fail0-20261022/`、`auto-suppress-budget-fail0-20261022/` 与
  早期两次同负载尝试 `auto-suppress-20261022/`、`auto-suppress-20261022-run2/` 一并保留，
  控制台输出 `evidence/p2/gates.log`、`evidence/p2/gates2.log`。
- 回归（同一构建）：`make clean` 全量重建 rc=0、`warning:` 0 行；`ctest` 6/6 0 跳过；
  `scripts/accept_sp3.sh` 连跑 6 轮 rc=0（`evidence/acceptance-sp3-20261022-r14/round1..6/`、
  `acceptance-sp3-strict-20261022-r14-round1..6.txt`）。产物 `ep ce49fca9…`、
  **`ep-executord ebfa88a4…`**、`ep-sched-loader d29aa3bb…`、`ep-schedctl af759004…`、
  **`ep_unit_tests ac6995d9…`**；全量重建与增量构建哈希相同。数据面 BPF 与 loader 未改。
- 仍未验证：抑制的**收益**（没有服务级 SLO 反馈，只能限速不能判断值不值）、参数扫描、
  预算窗口滑出后的真机行为（只有单测）、`fd_passing` 分支在位性单测。自动 apply 仍默认关闭。

## 服务级 SLO 反馈与抑制参数扫描（2026-10-22 追加）

- 产品新增/改动（全部 v2 自有代码，无新第三方依赖）：
  `include/ep/slo.hpp d1d6d3e70028…`、`src/core/slo.cpp b9ec2018809e…`（观测契约、服务半区、
  `evaluate_switch_outcome`）、`include/ep/executor.hpp ada81372ce34…`、
  `src/executor/executor.cpp 02daaf0c7329…`（`service.observe`、切换武装与确认窗口、status.service）、
  `src/executor/main.cpp 1c15b28d720a…`（`--auto-outcome-windows`）、
  `src/cli/main.cpp d1fad9ed5f74…`（`ep observe`）、
  `include/ep/sched_skill.hpp 77fe631b9c3e…`（修一个可读性缺陷：`SchedSkill::name()` 无论
  `--skill sched`（link FD 传递，生产路径）还是 `--skill sched-ref`（子进程对照）都返回
  `sched-ref`，于是 ready 日志与 `policy.status.skill` 把生产实例标成对照模式——现在按
  `paths_.fd_passing` 返回真实模式，`tests/integration_fd.sh` 加了断言）；
  `tests/unit/test_main.cpp c4e69e9c413b…`（`test_service_slo` 29 + `test_service_observe_rpc` 14，
  unit 合计 323 条断言）、`tests/integration_fd.sh f8b4336f9bf5…`（含 skill 模式断言）。
- 测量资产（不属于交付数据面）：`experiments/service_load_probe.c 8c4d43d41f67…`
  （GPL-2.0，`gcc -O2 -Wall -Wextra` 无警告；前台绝对 deadline 周期唤醒取 p99、后台固定等待之间
  完成工作量并记 overshoot）、`experiments/auto_apply_slo_feedback_sp3.sh 7f36e9b51f7c…`、
  `experiments/auto_apply_suppression_sp3.sh c4382360d627…`（逐门关 + 参数扫描用）。
  与 SP3 `/root/epv2-src` 逐字节一致。
- 证据：`evidence/p2/service-slo-20261022.txt` 与 `evidence/p2/service-slo-20261022/`
  （run1..run5：run1/run3 是脚本缺陷、run2 是单窗误判的直接证据、run4 是"观测断供即停"的
  真机证据、run5 是完整闭环；`slo1..slo5.log` 为各次控制台输出）；
  `evidence/p2/autopilot-suppression-20261022.txt` 第 9 节与
  `evidence/p2/suppression-sweep-{loose,medium,strict}-20261022/` + `suppression-sweep-20261022.log`。
- 回归：`make clean` 全量重建 rc=0、`warning:` 0 行；`ctest` 6/6 0 跳过；`scripts/accept_sp3.sh`
  **两轮各 6 轮全 rc=0**、每轮 `final.json` PASS：
  r15 是加入服务门控后的构建（`ep-executord def03c88…`、`ep_unit_tests e95697f8…`，
  `evidence/acceptance-sp3-20261022-r15/round1..6/`、`regress2.log`）；
  r16 是再修 `SchedSkill::name()` 之后的构建（**`ep-executord ddeb3803…`、`ep_unit_tests e9536dfb…`**，
  `ep 80da78b8…` 未变，`evidence/acceptance-sp3-20261022-r16/round1..6/`、
  `acceptance-sp3-strict-20261022-r16-round1..6.txt`、`regress3.log`、`ctest3.log`、`rebuild3.log`）。
  数据面两轮都未改：`ep-sched-loader d29aa3bb…`、`ep-schedctl af759004…`。
  SP3 上那 5 次 SLO 端到端运行与全部逐门关/扫描运行，跑的都是 r15 之前的同源码构建
  （`ep-executord` 与 r15 一致），即产品行为与本文档描述同源。
- 本轮明确写出的负面结论：同一状态连测 8 窗 p99 极差/均值 46%（背景进度 5.4%），大于策略声明的
  5% 退化预算 ⇒ p99 类约束目前不足以驱动自动判定；确认窗口只降低误判概率，未消除。
  自动 apply 仍默认关闭，性能矩阵仍未开始。
- 远端原始件：`SP3:/root/epv2-build/{r15,r16,r17}-round1..6/`、`SP3:/root/epv2-evidence/{auto-slo*,sweep-*,auto-suppress-*}/`。
  驱动脚本 `run_sweep.sh`、`run_gates*.sh`、`run_regress2..4.sh` 留在 SP3 `/root/epv2-src`，未入库（一次性外壳）。

## 服务观测分辨率口径与护栏（2026-10-22 追加）

- 测量资产：`experiments/slo_noise_floor_sp3.sh b343db7963b9…`（只量信号，不碰 sched_ext 与执行服务）。
  数据 `evidence/p2/slo-noise-floor-20261022/noise.tsv 3dede1b854c6…` + 控制台 `noise.log`：
  同状态重复取窗，5/15/30s × 5 次（排除首窗），p99 CV 3.8–20.2%、相邻窗最大相对变化 7.5–53.2%，
  背景进度相邻窗 ≤1.0%。**更长窗口不更安静**（30s 比 15s 差）。本会话中的状态表已用同一算法
  从该 tsv 复算核对过每个格子。
- 口径落文档：`docs/EXPERIMENT_PROTOCOL.md 5f129d32faea…` 新增 §5.1（窗口 15s、观测常驻、
  比较类判定先声明分辨率、绝对类不受分辨率豁免、前向延迟在线保护暂只用绝对量）。
  该节只约束在线反馈能否下判定，不改动离线实验矩阵的 block/60s 设定。
- 产品实现：`include/ep/slo.hpp d1cc295c129e…`、`src/core/slo.cpp ee26c889b113…`
  （`ServiceNoise`/`OutcomeVerdict`，敏感度不大于实测抖动 ⇒ 判定不执行并报 `not run`；判定顺序改为绝对量优先）、
  `include/ep/executor.hpp b5c60b8afa44…`、`src/executor/executor.cpp f4beab742d33…`
  （同档窗口历史每档最多 8 个；pending 期间的窗口不计入基线抖动；相差 >4× 的相邻窗视为状态变化；
  `policy.status.service` 增 `noise_p99_pct`/`noise_bg_progress_pct`）。
  抖动估计的第二、三条规则是 run7 逼出来的：它把同一档加载期 1.9ms 与撤载后 0.13ms 当成 90.3% 的"噪声"。
- 测试：`tests/unit/test_main.cpp 52b9b16c040b…`（`test_service_slo` 38 条，含跳过分支与
  "绝对量不被分辨率豁免"；服务侧合计 52 条，unit 总计 332 条（当时值，后续见 10-23 追加节））；
  实验脚本 `experiments/auto_apply_slo_feedback_sp3.sh e78b31595ac8…`
  （基线改为两窗、`SLO_WINDOW_S` 默认 15s、apply 显式 `--ttl-ms 900000`、探针前等档位非空、
  注入式确认段移除并改由真实判定 + 护栏自述断言）。
- 真机：run6（CLI 租约默认 30s TTL 在测量中途到期，自动通道在基线窗口内切换——运维事实，已修脚本）、
  run7（护栏生效但抖动估计被真实状态变化污染）、run8（`p99 噪声 39.6% ⇒ 不判`、`ADVERSE {}`、
  `CHECK_NO_SPURIOUS_CONDEMN=yes`、`CHECK_GUARD_EXPLAINS_ITSELF=yes`、饥饿注入仍 `BG_PROGRESS_FLOOR`）。
  证据 `evidence/p2/service-slo-20261022/{run6-lease-ttl-expiry,run7-noise-guard-90pct,run8-resolved}/`
  与 `slo6..slo8.log`；说明见该目录上级文档 §8。
  远端原始件：`SP3:/root/epv2-evidence/{slo-noise,auto-slo6,auto-slo7,auto-slo8}/`。
- 回归（护栏构建）：`make clean` 全量重建 rc=0、`warning:` 0 行（`rebuild4.log`）；`ctest` 6/6 0 跳过
  （`ctest4.log`）；`scripts/accept_sp3.sh` 6 轮全 rc=0、每轮 `final.json` PASS
  （`evidence/acceptance-sp3-20261022-r17/round1..6/`、`acceptance-sp3-strict-20261022-r17-round1..6.txt`）。
  产物 `ep 80da78b8…`（未变）、**`ep-executord f55517270d82…`**、**`ep_unit_tests e6565b318a6a…`**；
  数据面 `ep-sched-loader d29aa3bb…`、`ep-schedctl af759004…` 仍与 r13 起一致。
  r15/r16/r17 三轮分别是"加服务门控""修模式自报""加分辨率护栏"的构建，均各 6 轮通过。
- 由此明确的限制：前向 p99 的在线反馈目前是"已实现但被自身分辨率否决"的状态——按 §5.1 改用绝对量
  或多窗统计之前不得声称验证过前向退化；背景侧（进度下限/等待上限/进度比）是真机可拒动的那一半。

## 前向绝对上限与常驻观测（2026-10-23 追加）

- 策略契约扩展：`spec.safety.max_foreground_p99_ms`（可选，1..60000ms）。
  `design/schemas/scheduling-policy.schema.json`（可选属性，不进 `required`）、
  `src/core/policy.cpp b29407bda933…`（允许该键 + 范围校验，越界拒绝不钳制）、
  `src/core/slo.cpp 6b0f23e53404…`（`foreground_ceiling` 检查，`FOREGROUND_P99_EXCEEDED`；
  排在比较类之前、不受分辨率护栏豁免、不写入 `adverse_experts` ⇒ 超限消失即放行）。
  依据是协议 §5.1 第 3、4 条：实测 p99 相邻窗抖动 3.8–20% > 策略声明的 5% 相对预算，
  所以在线前向保护改用绝对量。
- 常驻生产者：`experiments/service_observer_sp3.sh 21e5d7e0e2fe…`（只读状态 + 探针 + `ep observe`，
  切换进行中跳过该轮而不是贴错标签）与驱动 `experiments/auto_apply_slo_ceiling_sp3.sh e537cb122abf…`。测试件：`tests/unit/test_main.cpp 0ebf089e9f18…`、`tests/integration_fd.sh 00c2541988e4…`、
  schema `design/schemas/scheduling-policy.schema.json 9915f709c1d7…`、契约文档 `design/INTERFACES.md f2e185106a7d…`。
- 真机（SP3，声明 1ms 上限，15s 窗）：实测 p99 0.129 → 1.545/1.282/1.201 → 0.139ms；
  超限时 `ep slo --expert latency-balanced` = `FOREGROUND_P99_EXCEEDED`，同期
  `features_fresh age_ms=153`、`cpu 51.4%`、`psi avg10 22.1`、`service_fresh age_ms=1002`
  全部合格 ⇒ 拒绝只来自服务上限；自动通道全程 0 次切换；5 轮常驻观测从未触发 `STALE`。
  `AFTER_DROP` 那行看到的是第 4 轮的超限观测（第 5 轮尚未落），所以"超限撤销后重新放行"
  本轮**没有**真机取样，只有无状态设计 + unit 双向断言支撑，证据文件 §4 明写。
- 测试：`tests/unit/test_main.cpp`（`test_service_slo` 44 条、服务侧合计 61、unit 总计 343）
  + `tests/integration_fd.sh`（实测超限被拒）+ 上述真机运行；
  run1 的打印块用 `json.load(sys.stdin)` 读脚本却把 JSON 当 argv 传入 → 三处异常，
  产品侧证据仍有效，run2 全量打印；两处脚本级修正记录在证据 §5。
- `fd_passing` 在位性覆盖边界（四处文档同步更正）：正分支由真内核集成层覆盖
  （`--skill sched` apply 断言 `VERIFIED`，`verify` 走同一个 `presence_ok`）；
  负分支在 unit 层不可达（`link_fd_` 只在 apply 里由 SCM_RIGHTS 收到），
  按 UNSUPPORTED-at-unit 记录，不加产品侧后门。
- 回归：`make clean` 全量重建 rc=0、`warning:` 0 行（`rebuild5.log`）；`ctest` 6/6 0 跳过
  （`ctest5.log`）；`scripts/accept_sp3.sh` 6 轮全 rc=0、每轮 `final.json` PASS
  （`evidence/acceptance-sp3-20261023-r18/round1..6/`、`acceptance-sp3-strict-20261023-r18-round1..6.txt`）。
  产物 `ep 80da78b8…`（未变）、**`ep-executord 436c0c78e023…`**、**`ep_unit_tests 20e120aadb5e…`**；
  数据面 `ep-sched-loader d29aa3bb…`、`ep-schedctl af759004…` 仍未变。
  r15/r16/r17/r18 四轮分别是"加服务门控""修模式自报""加分辨率护栏""加前向绝对上限"。
- 证据：`evidence/p2/service-slo-ceiling-20261023.txt` 与 `.../run1-broken-printers/`、
  `.../run2/`（含 `observer/observer.csv` 逐轮测量、`slo-at-breach.json` 完整检查列表）。

## 观测可靠性对账、恢复取样与矩阵 pilot（2026-10-23 追加）

- 产品：`include/ep/executor.hpp d2dbde5f2bc1…`、`src/executor/executor.cpp cac72ede4d6f…`，`policy.status.service` 新增消费端计数 `observations_accepted / observations_rejected /
  last_rejection`（`src/executor/executor.cpp`、`include/ep/executor.hpp`）。语义把两种状态分清：
  从未有观测 ⇒ 服务门 `not evaluated`（不拦）；有过观测后断供 ⇒ `SERVICE_OBSERVATION_STALE`（拦）。
- 观测者：`experiments/service_observer_sp3.sh bb75f0500c6a…` 重写为自带健康账——每阶段重试（默认 2 次）、
  `observer.errors`、`observer.heartbeat`、退出写 `observer.summary.json`
  （`cycles_attempted/ingested/skipped_no_policy/probe_failures/ingest_failures/coverage_pct/longest_gap_ms`）。
  驱动：`experiments/auto_apply_slo_ceiling_sp3.sh b4dc8a46ed39…` 加"先停生产者再取快照"的同刻对账与
  合规窗等待段。**修 run4 假告警的同时，run3 暴露观测者取档位用 `json.loads(sys.argv[1])`
  而 JSON 从管道喂入 → 107 个周期全部 skip**，两处都是脚本缺陷，产品行为未受影响。
- 真机（run5，`evidence/p2/service-slo-ceiling-20261023/run5/`）：`ingested=6 = accepted=6`、
  `rejected=0`、`coverage=91.73%`、`longest_gap_ms=16602`、`CHECK_TELEMETRY_NOT_STALE=yes`；
  绝对上限**解除后重新放行**取样成功（合规窗 p99 0.138758ms、age 2529ms ⇒ `code=none hold=none`）；
  同一运行最终快照 `switches=2 generation=5` 说明上限一解除、抑制参数在下限时通道立刻抖两次，
  因此"挡住切换"只声明为被超限观测覆盖时段内无动作（文档 §7.3 明写）。
- pilot：`experiments/pilot_matrix_sp3.sh 1d50a642dcd3…`（含分析变量改名、每块 pin 回收、keypool 填满三处修正）3 块 × A0/C/D，
  种子 20261023、真实臂序 `arm-order.txt`、redis 7.2.15 参数冻结、GET/SET 分开、BG 导出真实完成量、
  逐块 `env.json` + `SHA256SUMS` + 残留检查。数据 `evidence/p2/pilot-matrix-20261023/`（9 块齐），
  结论 `evidence/p2/pilot-matrix-20261023.txt`：**不进正式表**，抓出 8 条 harness 问题
  （窗长 0.4s、块内切换与窗同量级、goodput@SLO 不可算、客户端未标定、缺 A1/A2、
  manifest 时点错、BG 单位口径粗、前两版泄漏 pin + 分析脚本变量覆盖）。
  pilot 用的 `ep-executord 54b1500e…` 与 r19 验收构建一致，即测量臂与被验收的是同一份代码。
- 回归：`make clean` 全量重建 rc=0、`warning:` 0 行（`rebuild6.log`）；`ctest` 6/6 0 跳过
  （`ctest6.log`；`tests/unit/test_main.cpp 79422ea41795…`，unit 总计 343 条断言、服务侧 61 条）；`accept_sp3.sh` 6 轮全 rc=0、每轮 `final.json` PASS
  （`evidence/acceptance-sp3-20261023-r19/round1..6/`，
  `acceptance-sp3-strict-20261023-r19-round1..6.txt`）。产物 `ep 80da78b8…` 未变、
  **`ep-executord 54b1500e6408…`**、**`ep_unit_tests c0d2d2fcc0d9…`**；数据面 loader/schedctl 未变。
- 我自己造成的证据事故（如实记）：派生回归脚本时用 `sed` 替换名称，漏改 `acc-r17` → r19 六轮的
  控制台输出实际写在 `/tmp/acc-r17-*.log` 并覆盖了 r17 的远端副本。r17 的入库副本先前已取回，
  因此无损；r19 的六轮日志按时间戳（02:28–02:32）与轮次目录核对后改名入库。教训：
  一次性外壳脚本不要用 sed 派生，直接写全名。

## 逐请求开环负载、发生器标定与矩阵 harness（2026-10-23 追加）

- 新测量件（v2 自有，GPL-2.0，`gcc -O2 -Wall -Wextra` 无警告；不属交付数据面）：
  `experiments/redis_openloop_probe.c 30d28d6fbf28…`、`experiments/redis_rate_calibration_sp3.sh 8cb98eefbacb…`、`experiments/redis_matrix_sp3.sh f34ce473eafe…`；观测者同轮再修为 `experiments/service_observer_sp3.sh bb75f0500c6a…`。
  开环（固定到达率，服务变慢时 offered 不下降）、每条完成写 8 字节 ns 原始件、goodput@SLO 由逐请求判定计数，符合协议对"不得由分位数推算 goodput"的要求。
- 产品侧唯一改动是遥测计数（`observations_accepted/rejected/last_rejection`，`src/executor/executor.cpp cac72ede4d6f…`），构建与验收沿用 r19（`ep-executord 54b1500e…`、`ep_unit_tests c0d2d2fc…`、`ctest` 6/6、`accept_sp3.sh` r19 六轮 rc=0）；本轮未再改产品代码，因此不重跑验收，数据面 `ep-sched-loader d29aa3bb…`、`ep-schedctl af759004…` 仍未变。
- 标定（SP3，redis 7.2.15，无调度器）：5k/20k/40k/80k × GET/SET 各 20s，achieved/offered 全 100%、timeouts=0、stalls=0、发生器 CPU ≤0.64 核；80k 时服务端排队（p99 12.382ms、9.5% 超 5ms SLO）⇒ 旗舰 offered 率待 20k/40k/60k pilot 后冻结。
- 修掉的自造缺陷（都有原始数据佐证，见证据文件 §2 表）：level-triggered 注册 `EPOLLOUT` 造成空转（CPU 记账非单调）、发送后等到下一轮才 flush（5k rps 下 36% 请求落在 5.18–6.40ms 的悬崖，正是 5ms `epoll_wait` 超时）、`$-1` nil bulk 按长度 -1 处理导致 GET 一半错配、以及我在补丁过程中把 C 语言转义写成裸控制字节（文件里留下 CR/NUL，编译后比较语义被改）。
- harness 缺陷与修复：幂等键 `m-1-C` 短于契约 8 字符 ⇒ apply 被拒而 C/D 在**未挂载**状态下交出"完整数据"（新增 `require_attach`：核对 apply 状态与 `sched_ext.state`，不合格标 invalid 并中止）；`metadata.name` 含大写被 schema 拒；`policy_json` 参数错位使 `spec.source="5"`；A2 手写 `cpu.weight` 因根 cgroup 未委派 cpu 控制器被拒（改 systemd scope 并把 `cpu.weight=200` 读回为证）；两次运行共用一棵证据树互相删目录（加 `flock /run/lock/eulerpilot-v2-matrix.lock`，与验收脚本同族）。
- 证据：`evidence/p2/matrix-harness-20261023.txt` 与 `evidence/p2/matrix-harness-20261023/{matrix-smoke,matrix-a2,rate-calibration}/`（派生件 + 逐块 `SHA256SUMS`；97MB 的 `raw-*.bin` 与探针二进制留在 `SP3:/root/epv2-evidence/`，仓库只存派生结果与哈希）。A 臂 smoke 数字 n=1，**不是性能结论，不进正式表**；pilot 版数据与结论仍见 `evidence/p2/pilot-matrix-20261023.txt`。
- 清理与残留：每臂结束做所有权检查，收尾 `state=disabled pins=0`；远端一次性外壳 `killall_harness.sh`、`run_regress*.sh` 留在 `/root/epv2-src` 未入库（并记下一条操作教训：清理脚本与启动命令不能放在同一条 ssh 命令行里，`pkill -f` 会匹配到自己的命令行）。

## 观测断供可见性与消费者侧看门狗（2026-10-23 追加）

- 产品侧改动（v2 自有，GPL-2.0）：`include/ep/executor.hpp 147c7bd2df82…`、
  `src/executor/executor.cpp f2eef7f1065d…`。内容是把"断供"从只能在被问到的那一刻可见，变成事后可报告：
  `service_slo_input()` 记录 `observation_max_age_ms`（高水位）与 `stale_checks`（门控评估时看到过期的次数）并随
  `policy.status.service` 导出；该函数因此由 const 变为非 const（它在回答时记账，注释写明）。
  补报阈值与动作阈值分离：`observed_at_ms` 早于 `now-600s` 或早于 `now-10×window` 的观测**拒收**且不覆盖现值，
  而"能不能据此行动"仍由门控的 `2×window` 决定。二者是两套口径，不是同一个数。
  这两个计数是进程内遥测，**不写 WAL、不进 journal、重启清零**——它们不是事实效果，只是链路健康度。
- 测试：`tests/unit/test_main.cpp 0a40788e7e4c…`，unit 总计 **353** 条断言、服务侧 **71** 条
  （`test_service_observe_rpc` 27 条，新增 10 条覆盖过期拒收、高水位与计数随门控评估上升、拒收不覆盖现值、不动 generation）。
  看门狗本体是 `experiments/service_watchdog_sp3.sh 6e839662d191…`（只读 `policy.status`，不取租约、不写调度器），
  场景驱动 `experiments/service_observer_gap_sp3.sh 2e52cbb234c4…`，两者都是手工运行、不进 ctest。
- 真机（SP3，`evidence/p2/observer-gap-20261023/` + `observer-gap-20261023.txt`）：四阶段全部按预期，
  其中三条是这一层才能给的证据——恢复后 `age_ms=1044` 而高水位 26037ms/计数 12 仍在（控制面记得住断供）；
  dwell 钉在 3600000ms 时 hold 报的是 `slo denied: SERVICE_OBSERVATION_STALE`（证据层排在抑制层之前）；
  健康阶段 12 poll 0 违例（看门狗不误报）。附带量到生产端节奏约束（周期 `window+interval` 须严格小于 `2×window`），
  写入 `docs/EXPERIMENT_PROTOCOL.md` §5.1 第 6/7 条。
- 回归：`ctest` 6/6 通过（unit 11.63s，无 SKIP）；`scripts/accept_sp3_rounds.sh 027491ba002f…`（本轮新增的轮次驱动，
  把 r19 那次 sed 派生日志名造成 6 轮日志与轮次标签脱钩的事故变成脚本职责：tag 作参数、逐轮回读 `evidence:` 行改名）
  连跑 6 轮 rc=0（r20：`evidence/acceptance-sp3-20261023-r20/round1..6/`、
  `acceptance-sp3-strict-20261023-r20-round1..6.txt`、`acceptance-sp3-20261023-r20-driver.log`）。
  六轮 `SHA256SUMS` 逐字节相同并在 `/root/epv2-build` 下 `sha256sum -c` 通过；
  产物 `ep 80da78b8…` 未变、**`ep-executord a1ac7715c0ce…`**（r19 是 `54b1500e6408…`，本轮产品代码变了）、
  **`ep_unit_tests 8ec6fad9bb93…`**；数据面 `ep-sched-loader d29aa3bb…`、`ep-schedctl af759004…` 仍未变。
  观测者生产端 `experiments/service_observer_sp3.sh bb75f0500c6a…` 本轮未改，与上一节同一版本。
- 同步闭合：本地 → `SP3:/root/epv2-staging-20261023b/` → `/root/epv2-src/` 三方 sha256 逐一相同
  （executor.hpp `147c7bd2…`、executor.cpp `f2eef7f1…`、test_main.cpp `0a40788e…`、两份脚本 `6e839662…`/`2e52cbb2…`、
  驱动 `027491ba…`）；被覆盖前的 r19 源码副本留在 `SP3:/root/epv2-backup-20261023b/`。
- 我自己造成的缺陷（如实记）：场景驱动第一次运行把 apply 的成功状态写成 `OK`，实际契约返回 `VERIFIED`，
  脚本守卫立刻退出、未产生数据；修好后重跑才是上表读数（入库的是修复后的 `2e52cbb2…`，失败那次只有脚本缺陷这一条信息价值）。
- 边界：本轮不做性能测量（负载一个字节都没加，BG 读数来自探针自身循环）；看门狗只落文件与 stderr，
  没有外发告警通道；`stale_checks` 是"看到过期的次数"，不是断供时长，两者不得混用；
  多生产者并发摄取时 `ACCOUNTING` 规则如何定义尚未验证。

## 持续负载下的 runnable task stall、旗舰参数冻结与 r21（2026-10-23 追加）

- 缺陷（真机暴露，非推断）：旗舰参数 pilot 的 **6 个挂载块全部**在约 46s 被内核以
  `runnable task stall` 摘除调度器，受害者一律是 per-CPU kworker（dmesg 34.4–45.1s 报告，
  原始记录 `evidence/p1/stall-runnable-20261023/dmesg-eulerpilot.txt`）。配料分离：不加载 90s 0 次、
  仅后台 90s 0 次、仅服务负载 90s 第 46s 摘除 ⇒ 触发条件是服务负载本身。
  `stats` map（bpftool **只读** dump，执行服务仍是唯一写者）给出决定性一条：
  `enq_comp == dsp_comp`（447/447、63/63、230/230）⇒ 没有任务卡在我们的 DSQ 里等排空，
  "某个类 DSQ 没人 drain"这一类假设被数据否掉。
- 归因依据（读 SP3 内核源码，不猜）：`scx_bpf_dsq_move_to_local()` → `scx_dsq_move()` →
  `find_dsq_for_dispatch()`/`move_task_between_dsqs()` 在目标 CPU 不在 `p->cpus_ptr` 内时
  把任务**改投全局 DSQ**；全局 DSQ 只在某个 CPU balance 时被 `consume_global_dsq()` 排空，
  而绑定任务只有它自己那个 CPU 能取；`Documentation/scheduler/sched-ext.rst:228` 明确
  "唤醒 idle CPU 是调度器的责任"。绑定任务因此可以长期无人认领直到 30s 看门狗摘除。
- 修复尝试与结论（两步都留在档）：第一版只给非唤醒 enqueue 补 idle 唤醒
  （`kick_idle_for`），真机**仍然摘除**（45.056s，`stall-watch-fixed/`），且该路径 90s 只走 6–9 次
  （`enq_kick=6`）——它不是本案的解，但它是内核文档要求的正确改动，保留。
  第二版是真正的修复：`nr_cpus_allowed == 1` 的任务不进共享类 DSQ，直投自己 CPU 的
  `SCX_DSQ_LOCAL_ON` 并 `SCX_KICK_IDLE`，新增计数 `SP_STAT_ENQ_PINNED_LOCAL=38`（`enq_pinned_local`）。
- 验证（存活判据，不测性能）：同一失败配料 3 轮 × 90s ⇒ `POLLS samples=269 enabled=269`、
  dmesg 新增 stall `0` 条、`degraded=False`、`generation=1`（全程没有回退），
  `enq_pinned_local` 667→3290 且 `enq_comp == dsp_comp` 保持；修复前同机同负载 6/6 失败。
  边界写死在证据 §5：**不能声称 stall 类缺陷已穷尽**，且这一轮 `enq_lat/enq_cache/enq_bg` 全为 0
  （策略只有一个 FOREGROUND 目标，任务全落默认类），LAT/CACHE/BG 三条类路由未经此压测。
- 改动文件（v2 自有，GPL-2.0）：`sched/scx_schedpilot.bpf.c cbc3471d8617…`、
  `sched/intf.h 4cfe88ccbfaf…`、`sched/loader.c 381aee8c4761…`；
  BPF 对象 `eb53ca5d80a7…`、重新生成的 skeleton `sched/scx_schedpilot.bpf.skel.h d1c4e59804af…`。
  重建流程脚本化：`scripts/build_bpf_sp3.sh bce8047b1f97…`（此前这段流程只存在于
  `sched/README.md` 的散文里，无法按需复现——本轮必须重建 BPF 才暴露；上游 scx 头文件在本机
  不是 warning-clean，`scx/common.bpf.h:87` 一条可见性告警，脚本用 `-Wall` 不加 `-Werror`）。
- 单元层新增（测试侧改动，产品侧无后门）：`tests/unit/test_main.cpp f0bdc302c44a…`，
  unit 总计 **365** 条断言。`test_sched_skill_presence_branches`（12 条）把上一节记为
  "UNSUPPORTED-at-unit"的 `fd_passing` 负分支**更正为已覆盖**：`SchedPaths` 本来就是注入点，
  以 `fd_passing=true` 直接构造 `SchedSkill` 的自然状态就是"未持有 link FD"，因此
  `liveness()`/`verify()` 可在 unit 层直达该分支；同批覆盖 `sched-ref` 的 loader 消失分支、
  `verify_rolled_back` 未持 FD 时继续查内核状态、以及 applied 数据不可解析的拒绝。
- 旗舰参数冻结（`experiments/flagship_params.json 73e395a6d2e9…`，依据
  `evidence/p2/flagship-params-20261023.txt` 与 `evidence/p2/flagship-pilot-20261023/`）：
  offered **60000 rps**、SLO **5ms**（同时报 1/2/5/10ms）、BG 单位 **probe5s** + 下限 **140 单位/秒** +
  最长等待 **50ms**（schema 下限）、前向 p99 绝对上限 **10ms**、**20 block**、臂 A0/A1/A2/C/D、
  `seed=20261024`、配对 block bootstrap 10000 次。**pilot 数字（n=1/2）不进正式表**，
  只用于确认该点能分辨（40k 在 5ms 上贴顶：+0.02…0.12%）与分析管道可用。
- 新量具（不是结果）：`experiments/flagship_param_pilot_sp3.sh ff0a263953d3…`、
  `experiments/analyze_openloop_raw.py 0d061f2d94be…`、
  `experiments/analyze_matrix_blocks.py 0df9455482ab…`、`experiments/stall_watch_sp3.sh 416c987a8857…`、
  `experiments/flagship_matrix_sp3.sh c99b1c074f1a…`（预注册把**参数文件自身的 sha256** 记进
  `protocol.json`，并产出 `attempts.jsonl`/`per_run.csv`/`comparisons.csv`）。
  `experiments/redis_matrix_sp3.sh f2b78bfb76ed…` 本轮三处实质修改：
  ①每臂在挂载前用 `redis-cli --pipe` 建立同一 10 万键数据集并留 `populate.proof`（此前 GET 打的是空 keyspace，
  测的是最便宜的 NIL 回复，协议 §6.4 的"相同初始化"没有满足）；②阈值改由 env 传入且按 schema 类型取整
  （`max_bg_wait_ms`/`max_foreground_p99_ms` 是整数，写成 5000.0 会让策略校验拒绝——pilot 里就是这么把 C/D 两次拒掉的）；
  ③长寿命子进程加 `9>&-` 关闭继承的 flock fd，否则 bg 探针的孤儿孙进程会替下一次运行占着锁。
- 回归：`ctest` 6/6 0 跳过；`scripts/accept_sp3_rounds.sh 027491ba002f…` 连跑 6 轮 rc=0（r21：
  `evidence/acceptance-sp3-20261023-r21/round1..6/`、`acceptance-sp3-strict-20261023-r21-round1..6.txt`）。
  六轮 `SHA256SUMS` 相同：`ep 80da78b8…`、`ep-executord a1ac7715…`（与 r20 一致，控制面本轮未改）、
  **`ep-sched-loader 45bdc3c0d64c…`**（BPF 修复所致）、`ep-schedctl af759004…` 未变；
  `ep_unit_tests c25002db4cb5…`。
- 我自己造成的缺陷（如实记）：`analyze_openloop_raw.py` 的最近秩分位用了浮点下标；
  populate 的 RESP 流最初用文本 stdout 生成，在 Windows 上会把 `\n` 再翻译成 `\r\n`（变成
  `$64\r\r\n`），改成 `sys.stdout.buffer` 写字节后才做逐字节校验；pilot 驱动把 `apply` 成功状态
  写成 `OK`（实际 `VERIFIED`）导致首次运行 3 秒退出；`RATES=` 空值想跳过阶段 1，但
  `${RATES:-default}` 在空串时仍取默认值，白跑了 3 块。
- 同步闭合：本地 → `/root/epv2-staging-20261023b/` → `/root/epv2-src/` 共 13 个文件 sha256 全等
  （含 skeleton 与 `flagship_params.json`）；被覆盖前的 r19/r20 源码副本留在
  `SP3:/root/epv2-backup-20261023b/`。远端原始件：`SP3:/root/epv2-evidence/{pilot60b,pilot40b,
  stall-triage-*,stall-watch-*,flagship-smoke,flagship-main}/`（逐请求 `raw-*.bin` 约 29MB/个，
  不入仓库，块内 `SHA256SUMS` 已覆盖）。

## 旗舰正式矩阵与首轮性能数据（2026-10-23 追加）

- 运行：`experiments/flagship_matrix_sp3.sh`（SP3，06:59–10:46 UTC 单线程串行，flock 独占）。
  预注册文件 `protocol.json` 记录参数文件自身 sha256 `73e395a6d2e9…`（与仓库
  `experiments/flagship_params.json` 逐字节一致，实测核对）与被验收二进制：
  `ep 80da78b8…`、`ep-executord a1ac7715…`、**`ep-sched-loader 45bdc3c0…`**（stall 修复后构建）、
  `redis_openloop_probe 86b62652…`、`service_load_probe 95cb89e6…`、`redis_matrix_sp3.sh f2b78bfb76ed…`。
  20 block × A0/A1/A2/C/D、块内随机臂序（seed 20261024，`arm-order.txt` 为真实顺序）。
- 完整性证据先于结论：`attempts.jsonl` 100 条、valid 100、invalid 0；C/D 40/40 块结束
  `schedext.state=enabled`；A2 每块 `tilt.proof` 读回 `cpu.weight=200`；A0/A1/A2 全程 `disabled`
  （定义如此）；每块 26/26 个 BG 窗、`bg_under_populated` 全 no；超时合计 3 条（全在 A0 一块 GET）
  且按协议计入不剔除；收尾 `state=disabled pins=0`。
- **"原始件必须能重建表"这条第一次变成可核对的数**：`analyze_matrix_blocks.py` 从 `raw-*.bin`
  独立重算 goodput，与探针自报值在各自阈值上比较，**200 行最大偏差 0.019%**
  （`CHECK_RAW_REBUILDS_SUMMARY=yes`）。这是本轮新增的第二道独立校验（第一道是分位数重算）。
- 数据结论（配对、块内、bootstrap 10000 次、seed 20261024）：C vs A0 **+1.62% [+1.53,+1.82] 20/0**
  与 SET **+1.28% [+0.83,+1.83] 19/1**；D vs A0 +1.51%/+0.96%；p99 −47.9%/−32.5%；
  C vs A2 **+1.90% 20/0**；A1 vs A0 **−0.82% [−1.07,−0.36]**；D vs C SET **−0.27% [−0.48,−0.05]**
  且每块 `auto_switches=1`（20/20）、`observations_rejected=0`、无 adverse 档；
  BG 相对进度 0.997–1.001。**阈值敏感性同报**：@2ms 时 C SET −0.17%、D GET −0.00%、D SET −0.57%，
  即收益集中在 5ms 以上远尾——文档与汇报都按四档给，headline 不挑阈值。
- 工具新增/修正（量具，不是交付物）：`experiments/flagship_matrix_sp3.sh c99b1c07…`、
  `experiments/analyze_matrix_blocks.py`（本轮加入 `--also-thresholds` 一次遍历算多档、
  `goodput_vs_summary_pct` 独立复核、bg 窗数与 `auto_switches`/`outcome_pending` 列、
  绑定任务/块内配对 bootstrap；修了近秩分位的浮点下标与文本模式 stdout 的 CRLF 双重翻译）、
  `experiments/analyze_openloop_raw.py`（`med` 改为真中位数）。自测：两份分析器都先用合成块
  做过端到端 self-test（含 `records_vs_completed` 与重建一致性）再上真实数据。
- 一次我自己的失误（记档）：为了等分析结果，我在远端用了 `pkill -f "<含脚本名>"`——
  正是先前记录过的"清理命令与匹配文本同处一条命令行会自杀"这条教训，我又犯了一次，
  导致那次输出丢失；改为"先 pgrep 列 PID、再按显式 PID 处理、cat 单独一步"。
  教训本身不变，仍需靠把清理写进脚本来避免。
- 证据：`evidence/p2/flagship-matrix-20261023.txt`（结果与范围限定）与
  `evidence/p2/flagship-matrix-20261023/`（100 个块目录的派生件 + `protocol.json` +
  `attempts.jsonl` + `per_run.csv` + `comparisons.csv` + `analysis.txt`）；
  5.8GB 逐请求 `raw-*.bin` 留在 `SP3:/root/epv2-evidence/flagship-main/`，块内 `SHA256SUMS` 已入库。
- 范围：单机、单负载、单 SLO 设定、n=20。跨机与未见负载验证未做；BG 只证明"未受损"；
  四分类路由未接入控制面（`class_map` 只有 BACKGROUND 一条写入路径），故本轮数字属于
  NORMAL→COMP 路径与 BG，不能当作 LAT/CACHE 路由的收益。

## class 声明接入控制面与 LAT/CACHE 存活压测（2026-10-23 追加）

- 契约变更（v2 自有）：`design/schemas/scheduling-policy.schema.json 077f5748e308…` 给
  `spec.targets[]` 增加**可选** `class`（枚举 `NORMAL|LAT|COMPUTE|CACHE|BG`，仍拒绝未知字段）。
  生效类只有一个出口 `effective_runtime_class(role, declared)`（`src/core/target.cpp`）：
  未声明时 `BACKGROUND→BG`、其它角色不写 class_map —— **已有策略与已完成矩阵的语义逐字不变**。
  语义校验（`src/core/policy.cpp`）加两条不对称规则：`class=BG` 必须 `role=BACKGROUND`、
  `role=BACKGROUND` 只能声明 `BG`。这把项目规则"禁止把陌生任务降为 BG"从纪律变成结构约束。
- 生命周期（`src/skills/sched_skill.cpp`、`include/ep/sched_skill.hpp`）：运行时标记
  `bg_pids/bg_cgroups` → `classified:[{pid|path, class}]`（刷新要按声明的档重述，只记"曾被分类"无法重放）；
  `managed_bg_pids_` → `managed_class_`（pid→类）。刷新区分三种情况：类没变→保留、
  **类变了→就地重设**（先删后设会让任务在一个周期内以 NORMAL 运行）、撤下→`class-del`。
  `NORMAL` 不写条目，否则 PID 复用者会继承这条策略从未声明的分类。
  apply 回报的每个绑定新增 `class`/`effective_class`，写进 `docs/p1/ABI_FREEZE.md` 与
  `docs/p1/POLICY_BINDING_REPORT.md`。
- 测试层级：unit `test_policy_class_declaration`（真值表 + 四条拒绝），unit 总计 **375** 条、
  `ctest` 6/6 0 跳过、构建 0 warning（`-Wmissing-field-initializers` 是靠给新字段加默认成员
  初始化器消除的，不是用 `-w` 掩盖）；真内核 `tests/integration_fd.sh` 声明 `class=LAT` 后用
  `class-get` 从内核 map 读回 `LAT`，并断言事务回报的 `declared/effective` 为
  `[('', 'BG'), ('CACHE','CACHE'), ('LAT','LAT')]`。
- 存活压测（新件 `experiments/class_route_soak_sp3.sh`）：LAT/CACHE/BG 三档同时被喂，
  3×90s、60k rps ⇒ `POLLS 539/539 enabled`、`enq_lat 282 / dsp_lat 282 / run_lat 1.39M`、
  `enq_cache 762 / dsp_cache 762`、`enq_bg 3 / run_bg 59915`、`enq_pinned_local 7566`
  （上一轮 stall 修复的路径在这次仍持续生效）、dmesg 增量 0 条 stall、`degraded=False`、
  收尾 `pins=0`。数据 `evidence/p1/class-route-soak-20261023/`（`state.polls.csv` +
  挂载期间只读 `stats` dump）。第一次运行 `enq_cache=0`：驱动脚本在子 shell 里用 `$$`
  （= 调用方 shell 的 PID）搬 cgroup，真正该搬的进程没搬；改 `$BASHPID` 后才有流量。
  记这一条是因为在计数器上"没流量"与"没接上"完全同形。
- 回归：`ctest` 6/6；`accept_sp3_rounds.sh r22` 六轮 rc=0
  （`evidence/acceptance-sp3-20261023-r22/round1..6/`、
  `acceptance-sp3-strict-20261023-r22-round1..6.txt`、`acceptance-sp3-20261023-r22-driver.log`）。
  产物 `ep 80da78b8…`（未变）、**`ep-executord 5be26725…`**（控制面本轮改动）、
  `ep-sched-loader 45bdc3c0…`（**未变**：本轮没碰 BPF）、`ep-schedctl af759004…`（未变）、
  `ep_unit_tests 84640062…`。
- 本轮改动文件的本地↔SP3 哈希（逐个核对全等）：`include/ep/policy.hpp 4bd4f1ddcb4e…`、
  `include/ep/target.hpp 6dcdf707e384…`、`include/ep/sched_skill.hpp 46b5ab92a6ed…`、
  `src/core/policy.cpp 7167c13af5b2…`、`src/core/target.cpp 4bba2e8df1e7…`、
  `src/skills/sched_skill.cpp 169ccec4f771…`、`tests/unit/test_main.cpp c115491175eb…`、
  `tests/integration_fd.sh 372c648372bb…`、`design/schemas/scheduling-policy.schema.json 077f5748e308…`、
  `experiments/class_route_soak_sp3.sh 49a44ed6d416…`。
  被覆盖前的副本留在 `SP3:/root/epv2-backup-20261023b/`（含 `class-routing/` 子目录）。
- 未声称的：LLC 亲和下的 CACHE 分档路由、`SP_DSQ_SHARED`（控制面无开关）；
  以及**带 class 声明的性能数字**——已完成矩阵是"NORMAL→COMP + BG"形态的结果，
  要主张四分类路由的收益必须重跑矩阵。

## class 路由的配对消融（2026-10-23 追加）

- 运行：`experiments/flagship_matrix_sp3.sh` + `redis_matrix_sp3.sh 4ab9acb80b068eca…`（运行时的
  harness 版本，含 class 臂与统一 cgroup 放置；之后的版本加了消融臂，见下一节），
  预注册 `experiments/flagship_params_classrouting.json 2b25b8d127c4…`，
  被验收二进制 `ep 80da78b8…`、`ep-executord 5be26725…`、`ep-sched-loader 45bdc3c0…`（未变）、
  `redis_openloop_probe 86b62652…`、`service_load_probe 95cb89e6…`（以上均取自 `protocol.json`）。
  20 block × A0/C/Cc/D/Dc，12:58 预注册、16:40 结束。
- 完整性：`attempts.jsonl` 100/100 valid；80/80 个带策略块 `classes.proof` 为 `ok=yes`
  （`C → [("", ""), ("", "BG")]`，`Cc → [("BG","BG"),("LAT","LAT")]`）；100/100 `cgroup.proof`
  从 `/proc/<pid>/cgroup` 读回 `0::/matrix-scope/<块>-svc`；C/Cc/D/Dc 块结束 `enabled`；
  dmesg 新增 `runnable task stall` 0 条（全局计数保持 8，全部是此前的历史）；
  `CHECK_RAW_REBUILDS_SUMMARY=yes`；BG 26/26 窗齐全；D/Dc 每块各 1 次切换。
- 结论（**零结果**，块内配对、10000 次 bootstrap、seed 20261024）：Cc vs C = −0.01%
  [−0.29,+0.24]（GET）/ +0.05%（SET），胜负 10/10；Dc vs D = −0.18% [−0.45,−0.02]（GET），
  p99 4.87→5.35ms、p99.9 8.8→12.1ms；1/2/5ms 三档方向一致。Cc/Dc 相对 A0 的 +1.48%/+1.49%
  与无 class 的 C/D（+1.61%/+1.64%）无差别。⇒ class 声明可声明、可审计、可撤销，但**不是性能开关**；
  既有收益来自 expert 的切片/预算参数。机制解释只写成假设。
- 交叉核对：用 `per_run.csv`（由原始件重算）本地复算 A0 基线的四个对照，与 `analysis.txt`
  逐位一致（C +1.61 [1.39,1.79]、Cc +1.48 [1.25,2.06]、D +1.64 [1.35,2.02]、Dc +1.49 [1.29,1.74]），
  确认本地配对统计与分析器同一口径后才用于 Cc−C / Dc−D。
- 分析器本轮升级为**一次遍历多基线**（`--baselines "A0 C D"`），因为每多一个基线就要重读 5.8GB
  原始件；切换后用合成块自测（3 基线 × 24 行）验证输出结构。
- 同批发现并修复（**不在该轮矩阵的构建内，随后的 r23 构建包含**）：
  ①刷新把某 pid 的档位改成 `NORMAL` 时原实现是空操作（`ctrl_class_set(NORMAL)` 按设计不写），
  内核里旧档位会留到 TTL；②第一个修复补上后，新写的单测立刻又抓到第二层问题——原逻辑在**比较之前**
  就把新档位写进 `managed_class_`，于是"改档"永远看不出来，且新加入的 pid 永远不会被登记/撤销。
  最终把刷新改成三阶段（收集 desired → 按 pid 对账，NORMAL 走 `class-del`、其余就地重设 →
  撤销不再 desired 的条目），`managed_class_` 只记内核里真实存在的条目。
  `test_refresh_reclassifies_in_place`（13 条断言）覆盖：改档不得出现 `class-del`、改成 NORMAL 必须删除、
  删除行里不得出现 `--class`。unit 388 条、`ctest` 6/6、构建 0 warning。
  另修 `design/schemas/scheduling-policy.schema.json` 缺失 `max_foreground_p99_ms` 的契约漂移。
- 证据：`evidence/p2/flagship-classrouting-20261023.txt` 与同名目录（`protocol.json`、
  `attempts.jsonl`、`per_run.csv`、`comparisons.csv`、`analysis.txt`、逐块 `classes.proof`/
  `cgroup.proof`）；5.8GB 逐请求原始件留在 SP3，块内 `SHA256SUMS` 已入库。
- 范围：单机单负载 n=20；只声明 LAT（无 CACHE 工作集）；`llc_affinity=True`、`SP_DSQ_SHARED`、
  块内改档均未测。**这不是"class 路由无用"的一般结论**，只说明这条负载下它没有收益。

## 消融轮：服务门控与 BG vtime 惩罚（2026-10-23 追加）

- 运行：`flagship_matrix_sp3.sh` + `redis_matrix_sp3.sh 8324306d3f8a…`，
  预注册 `experiments/flagship_params_ablations.json 6a79181af3aa…`；10 block × A0/D/Dng/Dnv，
  探索性、不进旗舰表。构建 `ep-executord 23e23975…`（含刷新三阶段对账修复）、
  `ep_unit_tests 3a80b1c7…`、loader `45bdc3c0…` 未变。40/40 块 valid、dmesg 0 条新 stall、
  原始件重建 yes、逐块 `policy-adaptive.json` 读回确认三个臂的 safety/parameters 与设计一致。
- 结果：`Dng vs D` = +0.21% [−0.22,+0.60]（GET）/ −0.24%（SET）；`Dnv vs D` = +0.25%
  [−0.13,+0.65] / −0.30%。**门控臂的 null 是结构性的**：30 个自适应块 `last_hold` 全为
  `already in place`、0 次 `slo denied`、0 个 adverse 档，门从未拦截 ⇒ 本轮不构成门的价值证据，
  只说明"不触发的门没有可测开销"。`Dnv` 的 null 有信息量：取消 BG vtime 惩罚后前台与 BG
  进度（163.5 vs 163.5 单位/秒）都没有可测变化。
- **证据缺陷（如实记）**：本轮每块 `manifest.json` 的 `ablation` 字段是同一块**上一个臂**的值
  （`1-D` 记为 `no-service-gate`、`1-Dng` 记为 `none`），因为决策块原先位于 manifest 写入之后。
  已在 harness 中把决策前移（本文件追加时 `redis_matrix_sp3.sh` 的当前版本已修）；本轮证据目录
  按原样保留、不重写，归属以逐块 `policy-adaptive.json` 为准。
- 证据：`evidence/p2/flagship-ablations-20261023.txt` 与同名目录（`protocol.json`、
  `attempts.jsonl`、`per_run.csv`、`comparisons.csv`、`analysis.txt`）；逐请求原始件留在 SP3。
- 未做：no-preempt（无开关）、no-pmu（无集成，UNSUPPORTED）、no-llc（false 即默认）、
  no-router（即 C 臂）；以及**门在应当触发场景下的对照**（下一步）。

## 门控触发场景：行为量化，价值未测出（2026-10-23 追加）

- 新驱动 `experiments/gate_injection_sp3.sh`；构建 `ep-executord 23e23975…`、loader `45bdc3c0…`。4 轮 × (D, Dng)，顺序逐轮交替，dwell/candidate-hold 各 5s。
- **前置断言成立**：D 每轮 25 次 `slo denied: FOREGROUND_P99_EXCEEDED`（预算 1ms），切换推迟到第 26 次轮询（≈52s，注入已撤）；Dng 在第 6 次轮询（≈12s）切换 ⇒ **门把动作推迟约 40 秒**，且遇到碰巧达标的窗口仍会放行——门是延迟器不是否决器。
- **价值没测出来**：注入期 goodput 臂内波动 85–93%，逐轮配对 −4.6%/−17.4%/+170.8%/−23.7%；n=4、45s 窗在双稳均衡点里无法分辨百分级效应，证据文件禁止引用其中位数。
- 同轮测量事实：静息 p99 1.7ms 主要是 idle 退出延迟（96 个 1ms sleeper 使其降到 0.23ms）；1ms 探针 p99 对 CPU 抢占不敏感（16 spinner 仅 2.5–4.3ms，goodput 已掉到 1/3）；BG 进度在争抢下反而升高（163→176）；2ms 预算下门抖动。
- 驱动自造缺陷（已修并留档）：cgroup 读回 grep 错子串、`max_foreground_p99_ms` 写成浮点被 schema 拒、子 shell 用 `$$` 而非 `$BASHPID`、清理漏扫最后臂的 pin（已加兜底清扫）。
- 证据：`evidence/p2/gate-injection-20261023.txt` 与同名目录；原始件留在 SP3。未声称门有价值或有害。

## 切换延迟代价与测量件修复（2026-10-23 追加）

- **测量件 bug 与修复**：`experiments/redis_openloop_probe.c` 的摘要把 `offered_at_ms` 与 `finished_at_ms` 都写成同一行上的两次 `wall_ms()`，两者恒等；任何用其差算时长的消费者都会除零。`analyze_matrix_blocks.py` 有"elapsed 非法则退回声明窗口"的兜底，所以**此前所有 goodput 数字不受影响**（探针自身 goodput 用内部单调时钟，且与逐请求重建在各自阈值上一致到 0.019%），但**字段本身是假的**。已修（测量开始记 wall clock，摘要打印真实起止），重编译后校验 `finished−offered = 45002ms`（声明 45s）。探针哈希 `86b62652aed2…` → **`5f0213fd2dff…`**。
- 新驱动 `experiments/expert_gap_sp3.sh`：把"门控延迟的代价"拆成 `单位时间机会差 × 40s`，用固定 expert、不切换的静态对照量第一个因子；每轮交替顺序，并断言 `policy.status.auto_apply.active_expert` 等于本轮声称的 expert（否则该 run invalid）。构建 `ep-executord 23e23975…`、loader `45bdc3c0…`。
- **过载区间**（60k + 16 spinner，n=6）：goodput 差 +37.5% [−41.9,+134.4]、p99 −45.2% [−60.6,+60.9]，**CI 全跨 0、逐轮符号翻转**；同一臂 p50 在 2.3–14.4ms 之间跳、最大在途 1159–3001 ⇒ **双稳，不可测**。
- **稳定区间**（40k + 8 spinner，n=6）：两臂 goodput 均 39997–39999（100% offered）、p50 0.12–0.15ms、p99 1.11–1.29ms；差值 **goodput −0.00% [−0.01, 0.00]**、p99 +2.2% [−3.7,+7.3] ⇒ **40s 延迟的代价 ≈0–30 个请求（上界）**。结论：没有证据表明门控延迟造成吞吐损失；"过载下该不该切"要控制队列状态后才能变成可测问题。
- 证据：`evidence/p2/expert-gap-20261023.txt` 与同名目录（两组各 12 个 run）；原始件留在 SP3。

## phase-change 时间线与 noisy-mixed（2026-10-23 追加）

- 新驱动 `experiments/phase_change_sp3.sh`；构建 `ep-executord 23e23975…`、loader `45bdc3c0…`、探针 `5f0213fd…`。2 条时间线 × 4 相（quiet/noisy-cpu/noisy-mixed/quiet，各 60s），40k rps。臂 A0 = 裸服务（不启动执行服务），D = 执行服务 + autopilot（开局交租约，dwell 20s/候选保持 10s）。
- 结果：A0 在 mix 相 goodput 38.96k/39.08k（掉 0.6–1.0%）、cpu/mix p99 4–5ms、累计超预算 0.71%；D 四相 goodput ≥39.97k、p99 ≤1.2ms、累计超预算 0.02%；每条时间线只切换 1 次（r1 quiet、r2 cpu 相），noisy 段 `last_hold` 恒为 `already in place: latency-balanced`。恢复：goodput 无残留；p99 恢复方向不一致且未测进入稳态时间。
- **对照边界（必须一起引用）**：观测者只在 D 侧运行，故 D 的实际负载更高（结论偏保守），但 **PSI 不可跨臂比较**（D 的 quiet 相 `cpu.pressure some avg10` 23.8–24.4 主要来自观测者自身）；缺 C（固定 expert、无观测者）臂。
- 场景覆盖拼图：quiet 由旗舰覆盖（C vs A0 +1.6%）；noisy-cpu 由本轮 cpu 相与 expert-gap 的过载档覆盖；noisy-mixed = 本轮 mix 相；phase-change = 本轮；**mixed-foreground 未做**；**cross-machine UNSUPPORTED**（唯一验收级主机 SP3，SP4 因 cgroup v1/无 PSI 排除）。
- 证据：`evidence/p2/phase-change-20261023.txt` 与同名目录（逐相 probe/原始件/PSI 前后快照/2s 状态）；原始件留在 SP3。

## mixed-foreground 与 COMPUTE 词汇修复（2026-10-23 追加）

- **产品缺陷与修复**：策略的 class 白名单用 `COMPUTE`，而 `ep-schedctl parse_class` 只认 `COMP`；此前的 class 集成测试只声明过 LAT/BG，所以这条路径从未被走。首次给前台服务声明 `COMPUTE` 时 `class-set` 失败、整次 apply 被拒（`mixed-foreground` 的 D 臂第一次运行即因此失败）。修复放在唯一该翻译的边界 `SchedSkill::ctrl_class_set`；`tests/integration_fd.sh` 增加 COMPUTE 目标与 `('COMPUTE','COMPUTE')` 断言，真机跑通（`declared/effective = [('BG','BG'),('COMPUTE','COMPUTE'),('LAT','LAT')]`）。`src/skills/sched_skill.cpp 0f433c42a3ef…`、`tests/integration_fd.sh b022f9686c84…`。
- 新驱动 `experiments/mixed_foreground_sp3.sh`；3 轮 × (A0, D)，svc-A 30k/5ms/class LAT、svc-B 15k/2ms/class COMPUTE，外加 8 spinner 制造稀缺。`met` = 该服务 ≥99% 请求在预算内（本轮的操作性定义）。
- 结果：A0 joint **0/3**（svc-B 11495/15000、p99 4.02，0/3 达标；svc-A 3/3）；D joint **2/3**（svc-B 14860、p99 1.98；唯一 miss 是 r1 的 2.05ms 擦线；svc-A 3/3、p99 1.30）。**不能声称收益来自 class 分离**：D 与 A0 的差别同时含调度器、expert 参数与观测栈，缺 `D-no-class` 对照。
- 证据：`evidence/p2/mixed-foreground-20261023.txt` 与同名目录；原始件留在 SP3。
- 回归（COMPUTE 修复后）：`ctest` 6/6（`integration_fd.sh` 真机断言 `COMPUTE` 目标）；
  `accept_sp3_rounds.sh r24` 六轮 rc=0（`evidence/acceptance-sp3-20261024-r24/round1..6/`、
  `acceptance-sp3-strict-20261024-r24-round1..6.txt`）。产物 `ep 80da78b8…`（未变）、
  **`ep-executord bc3a163a7779…`**（本轮修复）、`ep-sched-loader 45bdc3c0…`（未变）、
  `ep-schedctl af759004…`（未变）、`ep_unit_tests d8d5f075…`。

## P1 退出门槛判定：协议 §8 fail-safe 矩阵与三项收尾修复（2026-10-24 追加）

- 新驱动 `experiments/fail_safe_matrix_sp3.sh` `5336ff7d23eb…`（SP3 上运行前后的文件哈希记在
  证据目录 `driver-SHA256SUMS`）。层级：故障恢复；不测性能。
- **本轮改动的源码**（SP3 `/root/epv2-src`，暂存 `/root/epv2-staging-20261024/fault`，
  覆盖前备份 `/root/epv2-backup-20261024/fault`，改前哈希逐一记在证据与安装输出里）：
  `include/ep/sched_skill.hpp f6bb90454b16…`、`src/skills/sched_skill.cpp 5864c63b10c4…`、
  `include/ep/executor.hpp cdbd28c27183…`、`src/executor/executor.cpp c6aa4d37422a…`、
  `src/executor/main.cpp ddead7654a4a…`、`tests/unit/test_main.cpp 5dce851651c3…`
  （改前 `63ed3b689e14…`，断言 388 → 426 条）。
- **修复 1（产品缺陷）**：执行服务收到停止信号只退出进程，回退与 pin 清理都不发生。
  新增 `Executor::shutdown()`（`src/executor/executor.cpp`、`include/ep/executor.hpp`、
  `src/executor/main.cpp`）。这不是洁癖：run2 实测到 S12/S15 各留下一个含 5 个 map pin
  （cfg/class_map/cpu_llc/stats/tg_stats）的实例目录，必须先手工清理才能重跑；协议 §8 明确
  要求"退出 loader/服务能否 detach 取决于 link 实际持有与 pin，验收读内核状态而非进程退出"。
- **修复 2（§8 的读回要求）**：撤销与回退改为由内核 map 自证——`revoke_managed(error,
  confirm_readback)` + `ctrl_class_get` + `residual_effects`；`verify_rolled_back` 的通过语
  改为 "…no class or cfg outlives the unpin"；孤儿回收从 `sched-runtime.json` 的 `classified`
  重建受管集合。刷新路径不开确认（成本随分类任务数增长，落在毫秒级刷新预算里）。
- **修复 3（外部改值）**：`SchedSkill::refresh` 开头一次 cfg 读回，不一致即写回并打印
  `[sched-skill] cfg drift corrected: …`；实测单次 map 读回 **5ms**。
  **披露**：该改动晚于旗舰矩阵构建，C/D/A1 的数字未在新构建上重跑。
- **run4 判定**：15 PASS / 1 FAIL，`state=disabled`、pin 根为空。FAIL 的 S14 是缺陷：状态目录
  ENOSPC 时 `ep rollback` 卡在前置 `lease.acquire`（写 `state.json` 失败，错误码误报
  `SCHEMA_INVALID`），`attempted=false`，策略仍挂在内核里；同一现场的 TTL 通道与 SIGTERM
  收尾都能撤除效应。证据 `evidence/p1/fail-safe-20261024.txt` `cc6801a47433…` 与同名目录
  （`results.tsv`、S01..S16.log、`daemon-excerpts.txt`、`SHA256SUMS`、`leftover-pin-dirs.txt`）。
- **两条非缺陷事实**：崩溃后 pinned map 由**同实例重启**回收（换 state 目录则无人回收，
  运维须复用目录或手工清理）；本内核 6.6.0-schedpilot-sp3 在 CPU 下线时直接注销 eulerpilot
  （dmesg `CPU 7 is now offline` → 34ms 后 `disabled (unregistered from the main kernel)`；
  `hotplug_seq 11→13`、`enable_seq` 不变），执行服务 3 次判定内检出并补偿回退。
- **驱动自身的两次误判已记档**（不记就等于没发生）：run1 把"pin 根非空"当全局脏机判定，
  一个场景的泄漏污染其后 12 条；S07 用 `policy_ttl_ms=6000`（schema 下限 10000）、S12 用
  `lat_slice_ns=111111`（helper 下限 250000），两处都让被测对象变成了校验器。run3 被首屏守卫
  SKIP，判定只取 run4。run1/run2 的完整树留在 SP3 `/root/epv2-evidence/fail-safe-20261024-run1`
  与 `-run2`，未回传仓库（内容与 run4 的失败原因一致，避免重复归档）。
- 判定文件：`docs/p1/P1_EXIT_REPORT.md` `9b3605c5b3ff…`。**P1 不关闭**，剩余阻塞项为
  S14 的存储故障分支与"跨域第二步失败"缺真机证据。
- 回归（同一份构建）：`cmake --build` 0 warning、`ep_unit_tests` 全绿、`ctest` 6/6、
  `accept_sp3_rounds.sh 20261024-r25` 六轮 rc=0
  （`evidence/acceptance-sp3-20261024-r25/round1..6/`、`strict-round1..6.txt`、`driver.log`）。
  产物 `ep 80da78b8…`（未变）、**`ep-executord 6e4b57da203f…`**（本轮修复）、
  `ep-sched-loader 45bdc3c0…`（未变）、`ep-schedctl af759004…`（未变）、
  `ep_unit_tests 3156968d3367…`。BPF 对象与 skeleton 未改动。

## S14 缺陷修复、S17 构造与 P1 判定改判（2026-10-24 同日追加）

上一条把 P1 判为"不关闭"，留下两条阻塞项。本轮把两项都关掉并**改判为达成**，判据与
不覆盖的边界写在 `docs/p1/P1_EXIT_REPORT.md`（改判过程在其 §7）。

- **改动源码**（SP3 `/root/epv2-src`，暂存与备份同前）：
  `include/ep/ownership.hpp d756f3c6e12a…`、`src/executor/ownership.cpp 4e73933cecc4…`
  （`acquire_lease` 增加 `persisted` 出参）、`include/ep/executor.hpp 69477b7c24ce…`、
  `src/executor/executor.cpp 83af6fb95fe2…`（`handle_lease` 授予"仅内存租约"+ `store_warning`）、
  `tests/unit/test_main.cpp 421a0e67d85f…`（断言 426 → **443**）。
- **修复内容**：状态目录写不下去时，`ep rollback` 曾卡在前置 `lease.acquire`
  （`REJECTED` + 误报 `SCHEMA_INVALID`、`attempted=false`、策略仍挂在内核里）。现按
  **方向不对称**处理：不可持久化的租约足以**拆除**已有效应（回 default fair 永远是安全方向），
  但新效应被 `RECOVERY_REQUIRED` 拒绝直到 reconcile 成功；`policy.status` 新增 `store_warning`，
  回退自身遇写失败如实回 `DEGRADED` + `rollback.status ∈ {AUDIT_PENDING, EFFECT_RESTORED}`。
  契约扩展登记在 `docs/p1/ABI_FREEZE.md` §6 第 8 条。
- **新 unit 测试的注入方式**：把 `state.json` 换成同名目录，让 `rename` 以 ENOTDIR/EISDIR 失败——
  任何 uid 都失败，**不需要产品侧注入口或后门**（延续"不为测试改产品"的边界）。
- **新真机条目 S17（两步写入的第二步失败）**：`--sched-ctl` 指向一个只拒绝 `class-set` 的
  包装器并记录每次调用。读数：步骤顺序 `get → set → class-set(被拒)`、
  `status=ROLLED_BACK`、`error.code=APPLY_FAILED` 且 message 点名被拒的一步、
  `generation 仍为 0`（未提交）、`state=disabled`、pin 根为空、目标继续推进；
  换回真实 helper 重试可正常 `VERIFIED` 并干净回退。**我最初的断言写错了**：
  以为该回 `REJECTED`，实际失败发生在已挂载之后，契约是补偿成功的 `ROLLED_BACK`；
  run7 因此失败，run8 按真实契约通过。严格说这是同域两次写，跨域 saga 仍属 P4。
- **运行史**：run5（16/16 判定通过但驱动 rc=1，因为在场景内 umount 自己挂的 tmpfs，
  守护进程还持有 socket/lock/journal → EBUSY）→ run6（改由 EXIT trap 卸载，16/16 PASS）→
  run7（加 S17，断言标签错）→ **run8（17/17 PASS、rc=0，收尾 `state=disabled`、pin 根为空）**。
  归档树：`evidence/p1/fail-safe-20261024/`（run8，权威）、`-run4/`（S14 FAIL 原始现场）、
  `-run6/`（修复后、加 S17 前的中间态）；run1/run2/run3/run5/run7 留在 SP3
  `/root/epv2-evidence/`。**一次归档失误也记在此**：拉 run6 时先把 tar 解到同名目录再改名，
  把本地 run4 的树覆盖了（远端 `-run4` 完好，已重新取回并改名到 `-run4/`）。
  驱动最终哈希 `experiments/fail_safe_matrix_sp3.sh ca8a850860d9…`（历次哈希逐轮记在
  各树 `driver-SHA256SUMS`）。
- **回归**：`cmake --build` 0 warning、`ep_unit_tests` 全绿（443 条断言）、`ctest` 6/6、
  验收 **r26 六轮 rc=0**（`evidence/acceptance-sp3-20261024-r26/round1..6/`、
  `strict-round1..6.txt`、`driver.log`），与 run8 同一份二进制：
  **`ep-executord db04f38513f5…`**、`ep 80da78b8…`（未变）、`ep-sched-loader 45bdc3c0…`（未变）、
  `ep-schedctl af759004…`（未变）。BPF 对象与 skeleton 仍未改动（`45bdc3c0` 自 r24 起一致）。
- **P1 判定达成，但边界照旧**：四项改动都晚于旗舰矩阵构建，C/D/A1 的数字未在新构建上重跑；
  adaptive/心跳/PMU 三条 UNSUPPORTED；越权改 class 条目不被纠正；stall 类缺陷未穷尽。
  文档：`docs/p1/P1_EXIT_REPORT.md 7ef28ff64f42…`、
  `evidence/p1/fail-safe-20261024.txt 9ba2f9afe27f…`（§7 为改判追加）。

## 内部标识脱敏（2026-10-24，把 v2 分支推到公开镜像之后）

推到 GitHub/Gitee 的 `v2` 分支后发现树里带着实机标识，做一次定向脱敏。这一节记录的是
**证据文件被改动**这件事本身，不把它写成"从来就没有"。

- 替换范围（值本身不在此复述，否则这条记录又把答案写回来了）：测试床 SP3 与 SP4 的
  内网 IPv4 地址 → `<SP3-IP>` / `<SP4-IP>`，跳板机别名 → `<JUMP-HOST>`，
  SSH 私钥文件名 → `<SSH-KEY>`。
  56 个文件、71 处替换，行级对称（+66/−66），全部落在 docs/README/evidence 的叙述文本里；
  仓库内的 `*.sh`/`*.py` 一处都没引用这些字符串，所以脱敏不影响任何脚本可执行性。
- 改完后重新解析所有被改动的 `.json`，**0 个解析失败**。
- **保留** VM 主机名 `schedpilot-sp3` / `schedpilot-sp4`（232 处/178 文件）：它们是证据目录
  与结论的命名锚点，不可路由，替换掉只会让"哪台机器测的"这条链断掉，安全收益为零。
- **未消除的部分**：`182d170` 到 `ef54663` 的历史 blob 里仍含原值，且这两个镜像在下一次推送前
  已经公开了这份内容。彻底清除需要重写历史并强推，属于破坏性操作，须另行确认。
- 未入库的逐块 dump（`evidence/` 下未跟踪部分）不含这些字符串，checked 4 处命中全在
  `.qoder-credits/`（已 gitignore 的工具产物）。

## R5 第一步：cgroup 效果域（2026-10-24 追加）

第二个效果域接入同一套事务骨架（快照→apply→verify→补偿→marker→启动恢复），不是旁路写接口。
证据：`evidence/p4/cgroup-domain-20261024.txt` + 同名目录（真机集成逐步输出、ctest 摘要、
环境快照、哈希）。

- 新增：`include/ep/cgroup_policy.hpp 1760ab023a6a…`、`include/ep/cgroup_skill.hpp 9054de3d7c26…`、
  `src/core/cgroup_policy.cpp b7324e9568b2…`、`src/skills/cgroup_skill.cpp 237f8c88dc4e…`、
  `tests/integration_cgroup.sh 512a2fbb9d80…`、
  `design/schemas/cgroup-policy.schema.json b6f94fdea71d…`、
  `design/examples/cgroup-protect-scope.json e06fdb234d82…`。
- 改动：`src/executor/executor.cpp 78fc46b74e04…`（kind/skill 域匹配 + 策略分派 + 恢复成功也记日志）、
  `src/executor/main.cpp 4d331e42bcf9…`（`--skill cgroup`）、
  `CMakeLists.txt 2c4dcf73ba20…`、`scripts/validate_design.py 10e8737667fa…`（新契约 + 4 个反例）、
  `scripts/check_sp3_acceptance.py 7da645e50c32…`（必测项 6→7）。
- **顺带做的一个去重**：调度域校验器里那套 JSON 字段检查（`check_keys/get_string/get_int/get_number/
  get_enum/valid_cgroup_path`）移进 `src/core/policy_json.hpp 9a1b635ee469…`，两个域共用同一份规则；
  `src/core/policy.cpp 9e44cd020e6b…` 删掉 109 行私有副本。理由不是整洁：两个契约如果对"未知字段、
  整数字段不接受浮点、路径必须规范化"这类规则各有一份实现，就会各自漂移出一个能写内核的口子。
- **两个由测试先发现的事实**（写在这里是因为它们决定了实现的形状）：写 knob 必须带 `O_TRUNC`
  （普通文件上短值覆盖长值会留残尾，kernfs 忽略该标志所以真机看不出来）；策略字段
  `cpu_weight` 与内部字段 `desired` 必须在唯一一处翻译，第一版没译，于是往内核写了 `0`。
- 回归：unit 断言 443 → **478**；ctest **7/7**；验收 **r27 六轮 rc=0**
  （`evidence/acceptance-sp3-20261024-r27/round1..6/`、`strict-round1..6.txt`、`driver.log`）。
  二进制：`ep-executord 042d74a3a8c1…`、`ep 80da78b86752…`（未变）、
  `ep_unit_tests 42f4df171799…`；`ep-sched-loader 45bdc3c0…`、`ep-schedctl af759004…` 未变，
  BPF 对象与 skeleton 未改动。
- **这一步不声称 R5 达成**：动作与恢复成立，但赛题要求的"真实 hook→决策→动作→恢复"还缺
  eBPF 事件触发端；且 `source` 仅 human、旋钮仅 `cpu.weight`、未测任何收益。

## 许可检查

2026-10-21 用户决定 v2 采用 **GPL-2.0**；根 `LICENSE` 为 GPL-2.0 全文，组件级说明见 `NOTICE`。
`sched/` 是 SchedPilot 派生的 GPL-2.0 组件；`third_party/nlohmann` 是 MIT；旧 EulerPilot 代码
没有复制进 v2，只作为参考。以下历史记录保留，用于说明迁移前的核查过程。

本轮读取到 EulerPilot 根 `LICENSE` 标明 **MulanPSL-2.0**；其README徽章仍写MIT，二者有不一致，后续迁移以实际文件许可/归属逐项核对，不复制徽章结论。单文件若有不同SPDX，也需单独记录。

SchedPilot 的 `bpf/scx_schedpilot.bpf.c`、`bpf/intf.h`、`loader/scx_schedpilot.c` 标明 `GPL-2.0`；`daemon/classifier.hpp` 等用户态文件标明MIT。本轮顶层清单未发现统一LICENSE文件。后续迁移保留原版权/SPDX并补齐相应文本及第三方来源；不能用一份新项目许可证覆盖所有来源。

本轮不决定整个v2的统一发布许可；在代码迁移和公开发布前完成文件级清单。许可核查记录是工程来源事实，不代表已完成法律审查。

## 外部工作

Linux sched_ext、scx、SchedCP/AgenticOS、MoS/ASA等一手链接及已确认边界见 [sources.md](docs/research/sources.md)。使用它们的架构或算法思路需在技术报告中归属，不把通用专家路由、MCP、门控或部署前验证本身作为新发明。

## 跨平台操作边界

本轮只在新项目写文件。后续连接SP3/宿主时，先检查实际目标/生效路径、仓库状态、差异与现有备份，在新目录构建/暂存；文件级diff/hash通过后再合并，不能整批覆盖旧实验环境。

## R5 事件触发链路(2026-10-25 追加)

新增组件与构建指纹(全部 SP3 `6.6.0-schedpilot-sp3`):`sched/events_trace.bpf.c` raw_tp
tracing 对象(`scripts/build_bpf_sp3.sh <src> <out> events` 重建;object SHA256
`433653b1d03299fd…`,skeleton `dd65f9a7bf70066…` 已入库,object 本身按调度器对象的
惯例不入库);只读事件泵 `ep-evtmon`(`c6b008b8aa4aebd6…`);执行服务 `ep-executord`
(`c70ab0934ea249a9…`)与 `ep`(`80da78b867526da1…`)为含事件链的重建;源码树聚合哈希
`e9d7f4b9addacfa5…`(排除 skel)。真机时间线与边界见
[evidence/p4/events-link-20261025.txt](evidence/p4/events-link-20261025.txt);
R5 动作端(cgroup Skill)见 2026-10-24 节。本轮改动晚于旗舰矩阵构建,矩阵重跑义务不变。
