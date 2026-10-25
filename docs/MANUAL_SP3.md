# SP3 运行与演示手册

面向评审现场与日常操作。环境基准:openEuler 24.03 LTS-SP3、内核 `6.6.0-schedpilot-sp3`
(自编译,sched_ext 可用)、cgroup v2、root。本文写于 2026-10-25,对应冻结构建
`370180b`(哈希清单见 [PROVENANCE.md](../PROVENANCE.md) 与
[evidence/p1/frozen-build-20261025.txt](../evidence/p1/frozen-build-20261025.txt))。

## 1. 构建配方(必须逐字照做)

```bash
cd /root/epv2-src
# BPF 对象(调度器 + 事件 tracing;输出 object/skeleton 哈希,进 PROVENANCE)
bash scripts/build_bpf_sp3.sh /root/epv2-src /root/epv2-src/sched all
# 全量构建。第二条参数是硬要求,见"陷阱"一节
cmake -S /root/epv2-src -B /root/epv2-build \
  -DEP_LIBBPF_STATIC=/root/ep-sched-build/build/obj/libbpf/libbpf.a
cmake --build /root/epv2-build -j"$(nproc)"
```

产物:`ep-executord`(唯一特权写者)、`ep`(CLI)、`ep-sched-loader`、`ep-schedctl`、
`ep-evtmon`(只读事件泵)、`ep_unit_tests`。

**陷阱 1(必踩一次)**:不带 `-DEP_LIBBPF_STATIC` 时,`ep-sched-loader` 链接系统
`libbpf.so` 且**启动即静默退出、日志为空**——症状像执行服务缺陷,实际是链接配置。
CMake 现在会打 WARNING;见到 WARNING 就停下重配。

**陷阱 2**:本机 cgroup 根和 `system.slice` 默认只委派 `memory pids`。裸 `mkdir`
出来的目录**没有** `cpu.weight`(打开报 EACCES 而不是 ENOENT)。凡要写 cpu.weight
的 cgroup,一律用 `systemd-run --scope -p CPUWeight=100 --unit <名>` 创建。

**陷阱 3**:任何 attach 类操作前必须 `cat /sys/kernel/sched_ext/state` 为
`disabled` 且 `/sys/fs/bpf/eulerpilot-v2` 为空(或不存在);不满足时所有测试/验收
都会拒绝,这不是故障。

## 2. 日常命令(CLI = `ep`,一切经 Unix socket)

```bash
ep status   --socket <sock>          # 实例/技能/世代/degraded/租约/观测/事件段
ep validate --file <policy.json> ... # 只读 dry-run:契约校验 + 目标解析 + digest
ep lease    --owner <id> --ttl-ms N  # 取租约(写操作的通行证)
ep apply    --file <policy.json> --idempotency-key <k> ...
ep rollback --of <action_id> --transaction <t> --snapshot <s> --policy-sha256 <h> ...
ep features | route | slo            # 感知链:FeatureWindow / 路由建议 / SLO 门
ep observe --file <obs.json> ...     # 服务观测摄取(只读方向,只能让门更严)
```

原则:**只有 `ep-executord` 写内核**;策略必须过 schema+语义校验;`source=human`
保留操作员权威,`rule-template`/`event-template` 各有额外的门。

## 3. 测试与验收(按层)

| 层 | 命令 | 说明 |
|---|---|---|
| 单元(572 断言) | `cd /root/epv2-build && ctest -R unit` | 无特权要求 |
| 集成 8 项 | `ctest --output-on-failure` | 含真内核挂载、cgroup 域、**R5 事件链六相时间线** |
| 一键验收 N 轮 | `bash scripts/accept_sp3_rounds.sh <标签> /root/epv2-build /root/epv2-src 6` | 强制 8 项无 SKIP + A/B/C liveness + 哈希 + 收尾断言 |
| fail-safe 矩阵 | `bash experiments/fail_safe_matrix_sp3.sh <executord> <ep> <loader> <out>` | 17 条故障注入 |
| class 漂移 | `bash experiments/class_drift_sp3.sh <executord> <ep> <loader> <out>` | 越权改/删被纠正 |

## 4. 实验矩阵

- 旗舰:`experiments/flagship_matrix_sp3.sh`,参数只从 `experiments/flagship_params.json`
  读(预注册),输出含 protocol.json/attempts.jsonl/per_run.csv/comparisons.csv。
  重跑/补充矩阵用 `PARAMS=<其它参数文件>` 换参(`flagship_params_barms.json`、
  `flagship_params_earms.json` 是已预注册的 B/E 补充)。
- mixed-foreground:`experiments/mixed_foreground_sp3.sh`,`MFG_ARMS="A0 Dnc"` 选
  no-class 对照。
- 分析:`experiments/analyze_matrix_blocks.py <out-dir> --baseline A0 --arms "..."`。

## 5. R5 事件链运行手册

```bash
# 1) 武装 cgroup 域执行服务(arm 是人工授权;watch 必须是活的 cgroup 目录)
ep-executord --state-dir <dir> --socket <sock> --skill cgroup --event-arm arm.json
# arm.json: kind=EventArm, spec.watch=<cgroup>, threshold_wait_ms, sustain/release_windows,
#           min_interval_ms, lease_ttl_ms, policy_ttl_ms, action.cpu_weight
# 2) 起只读事件泵(它不写任何东西;杀掉它=观测断供=执行服务的门冻结)
ep-evtmon --socket <sock> --window-ms 5000 --interval-ms 1000
```

行为:被看 cgroup 的 runnable 等待连续 `sustain_windows` 窗超阈值 → 守卫经标准事务
路径把 `cpu.weight` 提到 `action` 值;连续 `release_windows` 窗低于阈值、或 TTL 到期、
或泵死亡后的 TTL 兜底 → 自动补偿回原值。同路径 cgroup **删除重建**会使 arm 失效
(`WATCH_REBUILT`,需重启重新武装),不会把权限悄悄转给新对象。

## 6. 演示(demo_sp3.sh,约 5 分钟)

```bash
bash scripts/demo_sp3.sh /root/epv2-build/ep-executord /root/epv2-build/ep \
  /root/epv2-build/ep-sched-loader /root/epv2-build/ep-evtmon /root/epv2-evidence/demo-N
```

八步时间线:①执行服务与契约校验 → ②默认调度器基线 → ③策略生效(内核自证挂载+
class 读回)→ ④扰动下感知链实时应答(features/route/slo/observe)→ ⑤SIGKILL 崩溃
恢复 → ⑥cgroup 域 cpu.weight 写读 → ⑦R5 事件链(eBPF 测饥饿→升权→清除→补偿)→
⑧干净收尾断言。每步都有 `[demo]` 前缀输出与 `--` 说明行;结束打印 `[demo] PASS`,
证据目录自动归档。现场若某步失败,脚本保留现场并打印 FAIL 行——直接展示证据目录
本身就是项目方法论的一部分。

## 7. 故障排查速查

| 症状 | 原因与处置 |
|---|---|
| loader 启动即退、日志空 | 陷阱 1:缺 `-DEP_LIBBPF_STATIC` |
| mkdir 的 cgroup 写 cpu.weight 报 Permission denied | 陷阱 2:用 systemd-run scope 建 |
| 测试报 "sched_ext is not idle" | 有调度器挂着或 pin 残留;`cat /sys/kernel/sched_ext/state`,pin 属于谁就由谁回收,别手删别人的 |
| 验收报 pin 根非空 | 上一次异常退出;同实例重启执行服务会回收自己的 pin |
| 事件守卫不动 | 看 `policy.status` 的 `event.last_hold`:NONE=泵没起,STALE=泵死了,`below threshold`=条件未满足,WATCH_REBUILT=被看对象重建过 |
| `ep apply` 回 RECOVERY_REQUIRED | 执行服务 degraded;先 `transaction.reconcile` |

## 8. 证据与溯源

- 每次实验的权威记录在 `evidence/` 对应文件(含完整性与边界章节);
  `PROVENANCE.md` 记录逐轮二进制哈希。
- 逐请求原始件(GB 级)留在 SP3 `/root/epv2-evidence/`,仓库只入库汇总与
  `SHA256SUMS`。
- 比赛仓库(私有)直接推;公开镜像 `v2` 分支推送前必须重跑脱敏重写
  (配方见 PROVENANCE 与交接件 §6/§7)。
