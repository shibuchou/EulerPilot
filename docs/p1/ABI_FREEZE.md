# P1 控制面 ABI 冻结（v1alpha1 子集）

日期：2026-10-21。状态：**执行服务骨架、v2 自建 loader、FD 传递 link 所有权、
SchedulingPolicy schema/语义校验与 cgroup 目标绑定已实现；`sched` 在 SP3 通过真实
attach/detach 与崩溃回收测试，`sched-ref` 保留子进程对照。BPF ops 已改名 `eulerpilot`。**
本文冻结"已经实现并可测试"的部分；未实现的能力明确列为 NOT IMPLEMENTED，不当作契约承诺。

## 1. 已实现范围

| 能力 | 状态 | 说明 |
|---|---|---|
| ActionEnvelope 校验 | 已实现 | 字段来自 `design/schemas/action-envelope.schema.json` |
| `policy.apply` / `policy.rollback` | 已实现（mock、sched、sched-ref） | 真实调度走 sched/sched-ref，见下 |
| `sched`（FD 传递） | 已实现（SP3 实测） | loader 一次性 attach，SCM_RIGHTS 交 link FD；执行服务持有，崩溃时 FD 关闭即 detach |
| `sched-ref`（子进程对照） | 已实现 | 子进程 loader + PDEATHSIG；用于对照与测试 |
| 每实例 pin / ops | 已实现 | `/sys/fs/bpf/eulerpilot-v2/<instance>`；ops `eulerpilot` |
| BACKGROUND → class_map | 已实现 | 显式 `role=BACKGROUND` 的 pid 目标直接写入；cgroup 目标扫描 `cgroup.procs` 展开（上限 256）；均为 BG/EXTERN，执行服务每 2s 刷新（条目 TTL 5s）；FOREGROUND/NEUTRAL 不映射 |
| `spec.parameters` 写入 cfg map | 已实现 | 短命 `ep-schedctl` helper；写前快照、写后回读 |
| `policy.status` | 已实现 | 只读，不要求 lease |
| `policy.validate` | 已实现 | 只读 dry-run：策略校验 + target binding + digest |
| `feature.snapshot` | 已实现（P2 切片） | 只读；返回 FeatureWindow 的 samples/window_ms/sufficient 与 metrics（valid/source/samples/age/min/avg/max/missing） |
| `route.decide` | 已实现（P2 切片） | 只读 advisory；返回 expert/desired/switched/pending/reason/confidence/degraded/inputs；不写调度器 |
| `slo.evaluate` | 已实现（P2 切片） | 只读；按 FeatureWindow 与 expert 返回 allowed/code/reason/checks；缺指标、过期、PSI 压力超限保守拒绝 |
| 非人工来源 SLO 否决 | 已实现 | `source != human` 的 apply 先过 SloGate；拒绝返回 `SLO_DENIED`，无副作用；human 显式策略保留操作员权限 |
| SchedulingPolicy schema/语义校验 | 已实现 | 字段/范围/枚举/未知字段 + 心跳/TTL 关系 |
| cgroup/pid TargetBinding | 已实现 | cgroup: `O_PATH|O_NOFOLLOW` + `cgroup.controllers` + inode/device/boot_id/cgroup_id；pid: 持有 pidfd + starttime |
| `lease.acquire` / `transaction.reconcile` | 已实现 | P1 内部扩展，见 §6 |
| 唯一写者锁 | 已实现 | `flock` 独占，第二实例启动失败 |
| lease / fence / generation | 已实现 | 持久化到 `state.json` |
| lease 过期 fail-safe | 已实现 | 500ms 监控；自动 rollback 到 fair；`status.degraded=true`、`last_fallback`；失败记 RECOVERY_REQUIRED |
| policy TTL fail-safe | 已实现 | `safety.policy_ttl_ms` 到期同样自动 rollback；与 lease 共用监控/日志路径 |
| apply deadline 强制 | 已实现 | 入口、apply 后、verify 后检查 deadline；超时走补偿并返回 DEADLINE_EXCEEDED；rollback 不因 deadline 中断 |
| append-only journal | 已实现 | 每次 append 后 `fsync`；撕裂尾行启动时截断 |
| 事务状态机 | 已实现 | PREPARED→APPLYING→VERIFYING→VERIFIED→COMMITTED；失败 COMPENSATING→ROLLED_BACK/RECOVERY_REQUIRED |
| 崩溃 reconcile | 已实现 | 启动时按 journal + skill.reconcile 处理未完成事务 |
| 孤儿资源回收 | 已实现 | `Skill::recover_orphan`，处理已 COMMITTED 但进程崩溃的 loader/pin |
| 幂等 | 已实现 | 同 key 同内容返回原结果；同 key 不同内容拒绝 |
| Unix socket 控制通道 | 已实现 | 0600，`SO_PEERCRED` 校验 uid |
| v2 自建 loader | 已实现（FD 传递 + 子进程对照） | `sched/` 构建 `ep-sched-loader`；`sched` 模式交 link FD，`sched-ref` 保留子进程；ops `eulerpilot` |
| cgroup / TC / 事件链 | **NOT IMPLEMENTED** | 不写 cgroup、不挂 TC/XDP |

## 2. 线路格式

写操作沿用设计契约的 envelope，但 schema 是 `additionalProperties: false` 且不包含策略正文，
因此请求把 envelope 与正文并排放在同一 JSON 对象里：

```json
{
  "operation": "policy.apply",
  "envelope": { "...action-envelope 的 11 个必填字段..." },
  "policy": { "...SchedulingPolicy..." }
}
```

测试模式用 `"intent": {"value": 42}` 代替 `policy`，仅用于 mock skill；生产模式只接受 `policy`。
`policy_sha256` 必须是**正文的规范 JSON**（键排序、无多余空白）的 SHA-256；文件哈希另存，
两者不能混用。执行服务会重算并拒绝不匹配。

只读/控制操作不使用 envelope：

```json
{"operation": "policy.status", "request_id": "req-..."}
{"operation": "policy.validate", "request_id": "req-...", "policy": {"...SchedulingPolicy..."}}
{"operation": "lease.acquire", "request_id": "req-...", "lease_owner": "agent-...", "lease_ttl_ms": 30000}
{"operation": "transaction.reconcile", "request_id": "req-..."}
```

每次连接只处理一个请求：客户端写一行 JSON，服务端回一行 JSON 后关闭。上限 1 MiB。

## 3. 结果对象

在契约字段之外，P1 增加了三个寻址字段，回滚需要它们：

```json
{
  "request_id": "req-...",
  "transaction_id": "txn-...",
  "action_id": "act-...",
  "snapshot_id": "snap-...",
  "status": "VERIFIED",
  "observed_generation": 1,
  "policy_sha256": "...",
  "target_bindings": [],
  "checks": [{"name": "probe", "ok": true, "detail": "..."}],
  "rollback": {"attempted": false, "status": "NOT_NEEDED", "detail": ""},
  "error": null
}
```

状态集合沿用契约：`PLANNED/APPLIED/VERIFIED/REJECTED/ROLLED_BACK/DEGRADED/RECOVERY_REQUIRED`。
只读成功用 `status: "OK"`（不属于 ActionResult）。

`target_bindings` 在真实 apply 时填充，每项包含 `kind/path/role/role_source/identity/
inode/device/cgroup_id/pid/starttime/boot_id`。cgroup 的 `identity` 为 `cgroup2-kernfs`，
`cgroup_id` 是内核 cgroup id（0 表示该挂载点不可用）；pid 的 `identity` 为
`pidfd+starttime`，带 `pid` 与 `starttime`；pidfd 只在进程内 bindings 生命周期持有，不序列化。
空数组表示没有目标（测试模式或尚未解析），不是"已验证无目标"。

错误码在契约集合上增加：

- `APPLY_FAILED`：skill.apply 失败且已尝试补偿；
- `DEADLINE_EXCEEDED`：apply 入口/apply 后/verify 后超过 deadline，已补偿；
- `SLO_DENIED`：非人工来源策略未通过 SLO 门控，无副作用；
- `RECOVERY_REQUIRED`：执行服务处于 degraded，拒绝新 apply；
- `IDEMPOTENCY_CONFLICT`：同 key 不同内容。

## 4. 幂等与 generation 语义

- 幂等摘要 = `sha256(canonical_json({"operation": ..., "payload": ...}))`。
  **不含** request_id、transaction_id、lease、fence、expected_generation。
  这些是传输/控制状态，重试时会变化；把它们纳入摘要会让正常重试被误判为冲突。
- 同 key 同摘要：直接返回已持久化的结果，不重新执行、不推进 generation。
- 同 key 不同摘要：`REJECTED / IDEMPOTENCY_CONFLICT`。
- 新意图必须使用新 key；执行服务不推断"内容相同就是同一次意图"。
- `expected_generation` 必须等于执行服务当前 generation；不匹配返回 `GENERATION_CONFLICT`，
  不自动重试覆盖。成功 apply 后 generation +1；成功 rollback 后 generation +1。
- 补偿性回滚（apply 失败后的自动回滚）不推进 generation；用户显式 `policy.rollback` 推进。

## 5. 事务与 reconcile

journal 记录类型：`PREPARED, APPLYING, VERIFYING, VERIFIED, COMMITTED, COMPENSATING,
ROLLED_BACK, RECOVERY_REQUIRED, ROLLBACK_PREPARED, RESULT`。

- `PREPARED` 必须包含 `snapshot`、`intent`、`snapshot_id`、`policy_sha256`、`generation_before`，
  先 fsync 再调用 skill.apply。
- 真实 apply 在 `PREPARED` 之前完成策略 schema/语义校验与 target 解析；写 `PREPARED` 前再
  `revalidate` 一次（boot_id + inode/device），失败返回 `TARGET_STALE`，不进入事务。
  `PREPARED.data.target_bindings` 保存本次绑定的身份快照。
- 策略含 `spec.parameters` 时：attach 后先读取 cfg 快照，再写入参数并回读校验；快照与写后值
  记入 runtime 与 applied data。rollback/recover_orphan 在 detach 前先恢复快照。
- 启动时对未完成事务（最后记录为 PREPARED/APPLYING/VERIFYING/COMPENSATING/ROLLBACK_PREPARED
  或 RECOVERY_REQUIRED）调用 `skill.reconcile`：
  - 能判定并补偿 → 追加 `ROLLED_BACK`（`generation_advanced=false`）；
  - 无法判定/外部改动 → 追加 `RECOVERY_REQUIRED`，执行服务进入 degraded，新 apply 返回
    `RECOVERY_REQUIRED`，直到显式 `transaction.reconcile` 解决。
- 用户 rollback 的 `ROLLED_BACK` 带 `generation_advanced=true`，`max_committed_generation`
  只把这类记录计入 generation。
- 补偿只恢复仍由本服务控制的值；发现外部改动时报告冲突，不覆盖。

## 6. 明确标注的 P1 内部扩展

以下不在原 Tool 协议列表里，是骨架阶段为了可测试性加的，P2 需要决定保留还是收敛：

1. `lease.acquire`：客户端取得 owner + fence + expires；fence 单调递增，旧 fence 立即失效。
2. `transaction.reconcile`：显式重试未完成事务与孤儿资源，不要求 lease（受 socket uid 限制）。
3. 结果中的 `transaction_id/action_id/snapshot_id`：用于 `rollback_of` 寻址。
4. mock skill 与 `--test-mode`：仅测试用；`--skill sched` 选择 FD 传递模式，
   `--skill sched-ref` 选择子进程对照模式，`--loader PATH` 指定二进制。
5. `Skill::verify_rolled_back(snapshot, deadline)`：回滚后的域语义校验。不能用"整快照比对"，
   因为 sched_ext 的 `enable_seq` 是单调计数。
6. `Skill::recover_orphan(error)`：执行服务启动时回收不属于未完成事务的孤儿资源
   （例如已 COMMITTED 的 apply 留下的 loader/pin）。

## 7. 权限与所有权

- 执行服务启动即持 `flock(state_dir/executor.lock)`，第二实例直接失败。
- socket 权限 0600；默认只接受与执行服务相同 uid 的 peer（root 服务即 root 客户端）。
  `--allow-any-uid` 仅用于测试，不得用于交付。
- 所有 apply/rollback 必须带当前 lease owner + fence；generation 必须匹配。
- 执行服务崩溃时 flock 释放，但**不会**自动删除 pin/cgroup；重启先 reconcile，不"发现旧 pin 就清空"。

参考 SchedSkill 的额外所有权规则（`docs/p1/SCHED_SKILL_REPORT.md`）：

- 子 loader 设置 `PR_SET_PDEATHSIG=SIGTERM`：执行服务被 SIGKILL 时 loader 退出并 detach 回 fair，
  但 pin 会留下，由启动时的 `recover_orphan` 清理。
- `sched-runtime.json` 记录 pid + `/proc` starttime + pin inode/device；停止进程前核对身份，
  清 pin 前逐个核对 inode，发现外部 pin 即报告冲突，不覆盖。
- `preflight` 拒绝在 `schedpilotd` 运行时 attach：避免同一环境出现第二套控制面与旧 pin map。
- pin 命名空间为每实例 `/sys/fs/bpf/eulerpilot-v2/<instance_id>`；`sched` 模式执行服务通过
  SCM_RIGHTS 持有 struct_ops link FD，崩溃时 FD 关闭即 detach；`sched-ref` 子进程模式仍用
  PDEATHSIG。
- 必须使用内核树静态 libbpf ≥1.3；SP3 系统 libbpf 1.2.2 会在加载 struct_ops 对象时 SIGSEGV。
- cfg 写入由执行服务 fork 的短命 `ep-schedctl` 完成；它不是长期第二写者，参数范围在 helper 内
  再次校验。执行服务仍不直接持有 cfg map FD。

## 8. 测试覆盖与边界

`tests/unit/test_main.cpp` 覆盖：SHA-256 向量、规范 JSON、RFC 3339 解析、journal 撕裂尾行、
flock 第二实例、fence 递增/失效、apply→重放→幂等冲突→stale fence→generation 冲突→rollback、
外部改动触发 RECOVERY_REQUIRED、合成崩溃记录后的 reconcile、UNSUPPORTED skill、用
fake loader/fake sched_ext 目录跑通 sched-ref 的 apply/rollback/孤儿回收，以及 SchedulingPolicy
的 11 类非法输入和 cgroup v2 target 解析/revalidate。

`tests/integration.sh` 覆盖：真实 daemon + CLI、socket 往返、apply/replay/rollback、
SIGTERM 重启后 generation 保持、第二实例拒绝、合成 PREPARED/APPLYING 后的启动 reconcile。

`tests/integration_sched.sh` 覆盖：先 `ep validate` 校验真实策略与 cgroup 绑定，再执行真实
sched_ext attach、rollback detach、`SIGKILL` 执行服务后 PDEATHSIG 让调度器退出、重启后
孤儿 loader/pin 回收、generation 保持。缺 root 或 loader 时返回 77（CTest SKIP），
不会在普通开发环境误跑。

**不证明**：真实 sched_ext 数据面、cgroup 写入、策略 schema 语义校验、target binding、
性能、内核 ABI 兼容。这些仍按阶段门槛另行验收。

## 9. 构建与依赖

- C++20，CMake ≥ 3.20；Linux/POSIX。
- 唯一第三方依赖：`third_party/nlohmann/json.hpp` 3.11.3（MIT，SHA256 见该目录 README）。
- 目标二进制：`ep-executord`（特权写入服务，`--skill sched|mock`、`--loader PATH`、
  `--sched-ctl PATH`）、`ep`（CLI）、`ep-sched-loader` 与 `ep-schedctl`
  （GPL-2.0，sched_ext 数据面；见 `sched/README.md`）。
- 开发验证：WSL Ubuntu 24.04 + g++ 13.3 + CMake 3.28。
- 目标验证：openEuler 24.03-LTS-SP3 + g++ 12.3 + CMake 3.27（见 P1 报告与 SchedSkill 报告）。
