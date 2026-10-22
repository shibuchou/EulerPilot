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
  `evidence/acceptance-sp3-20261021.txt`、`evidence/acceptance-sp3-strict-20261022.txt`、`evidence/acceptance-sp3-20261022/`、`evidence/acceptance-sp3-strict-20261022-r3.txt`、`evidence/acceptance-sp3-20261022-r3/`、`evidence/p1/helper-budget-20261022.txt`、`evidence/acceptance-sp3-strict-20261022-r4.txt`、`evidence/acceptance-sp3-20261022-r4/`、`evidence/p1/wal-hardening-20261022.txt`、`evidence/acceptance-sp3-strict-20261022-r5.txt`、`evidence/acceptance-sp3-20261022-r5/`、`evidence/p1/active-bindings-20261022.txt`、`evidence/acceptance-sp3-strict-20261022-r6.txt`、`evidence/acceptance-sp3-20261022-r6/`、`evidence/p1/fairness-pilot-20261022.txt`、`evidence/p1/fairness-sp3-20261022.json`、`evidence/p1/fairness-sp3-20261022.stats.txt`。
- `--skill sched` 采用 FD 传递：loader 一次性 attach 后把 struct_ops link FD 通过
  SCM_RIGHTS 交给执行服务并退出；执行服务崩溃时 FD 关闭即 detach。`sched-ref` 保留
  子进程模式用于对照与测试。

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
