# P1 策略校验与目标绑定报告

日期：2026-10-21；状态：**SchedulingPolicy 的 schema/语义校验与 cgroup v2 目标绑定已实现，
并在 SP3 上通过真实 apply/rollback 验证。** 这是用户态安全边界，不涉及 v2 自有数据面或性能。

证据：[sp3-policy-binding.txt](../../evidence/p1/sp3-policy-binding.txt)。

## 1. 为什么先做这个

在此之前，`policy.apply` 只校验 JSON 与 digest，任何带 `spec.mode` 的对象都能触发内核
attach。策略里的 targets、参数范围、心跳/TTL 关系都没有检查。这违反 fail-closed 原则，
所以先补安全边界，再考虑替换 loader。

## 2. 已实现的校验

`validate_scheduling_policy()`（`src/core/policy.cpp`）逐字段校验 v1alpha1 契约，并拒绝未知字段：

| 部分 | 规则 |
|---|---|
| 根对象 | 只允许 `apiVersion/kind/metadata/spec`；`apiVersion` 必须匹配；`kind=SchedulingPolicy` |
| metadata | 只允许 `name`；`^[a-z][a-z0-9-]{0,62}$` |
| spec | 只允许 7 个字段；`scope=scheduler-instance`；mode/expert/source 枚举 |
| targets | 1–64 个；kind=cgroup；路径必须是规范化的 `/sys/fs/cgroup/...`，拒绝 `.`/`..` 段；role 枚举；`role_source=explicit`；路径不得重复 |
| parameters | 7 个字段全部必填；slice 250µs–10ms；preempt 100µs–2ms；bg_vtime 50–400%；llc_affinity 布尔 |
| safety | 12 个字段全部必填；各范围按 schema；`allow_affinity_change` 必须为 false；fallback 枚举 |

schema 无法表达的语义约束：

- `heartbeat_timeout_ms > heartbeat_period_ms`
- `policy_ttl_ms > min_dwell_ms`
- `max_observation_age_ms <= heartbeat_timeout_ms`
- `bg_progress_window_ms <= policy_ttl_ms`

## 3. cgroup v2 目标绑定

`resolve_targets()`（`src/core/target.cpp`）：

- 以 `O_PATH|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW` 打开每个目标；
- 必须存在 `cgroup.controllers`，否则不是 cgroup v2；
- 记录 `path + kernfs inode + device + boot_id + role`；
- 用 `name_to_handle_at()` 读取 cgroup2 kernfs 的 8 字节 handle，作为内核 `cgroup_id`
  （0 表示该挂载点不可用）；SP3 上 `/sys/fs/cgroup/system.slice` 得到 `cgroup_id=69`；
- pid 目标：`pidfd_open()` 后持有 pidfd，读 `/proc/<pid>/stat` 的 starttime，记录 pid + starttime；
- 写入前再次 `revalidate_targets()`：boot_id 不变、路径 inode/device 不变、
  原 `cgroup_id` 非 0 时必须与当前值一致；pid 目标用持有的 pidfd 检查存活并核对 starttime，
  进程消失或 starttime 变化即 `TARGET_STALE`；
- 绑定写入 journal 的 `PREPARED` 记录，并出现在 `ActionResult.target_bindings`。

新增只读操作 `policy.validate`：不取 lease、不写系统，返回策略摘要、规范 digest 与
target bindings，供 CLI dry-run 使用。CLI 增加 `ep validate --file POLICY.json`。

## 4. 实测（SP3）

```text
ep validate valid.json   -> status OK, mode=class, expert=latency-balanced
                            target /sys/fs/cgroup/system.slice inode=69 device=26
                            cgroup_id=69, identity=cgroup2-kernfs
ep validate invalid-mode -> REJECTED / SCHEMA_INVALID / "invalid value 'turbo'"
ep validate traversal    -> REJECTED / SCHEMA_INVALID / "must be a normalized ... path"
ep apply valid policy    -> VERIFIED, checks: policy + targets(1), state=enabled
ep rollback              -> ROLLED_BACK, state=disabled, pins removed
```

SP3 `ctest` 4/4 通过（unit / integration / integration_sched / integration_fd）；
`integration_fd` 额外断言 apply 结果的 `target_bindings[0].cgroup_id > 0`，以及
pid 目标的 `pid=1`、`starttime>0`。

单元测试覆盖：缺失字段、未知字段、非法 name、非法 mode、越界 slice、重复 target、
路径穿越、心跳超时≤周期、TTL≤驻留、观测年龄>心跳超时、允许修改亲和性。

## 5. 明确边界

- **cgroup/pid 身份已实现**：cgroup 有路径 + 内核 cgroup id；pid 有持有的 pidfd + starttime，
  pidfd 在 bindings 生命周期内保持打开。
- **作用域仍是 scheduler-instance**：targets 用于授权与审计，loader 还没有
  按 target 写 class_map；真正的 per-target 参数要等 v2 数据面。
- **不校验运行特征**：策略里的 expert 只是白名单字符串，是否匹配当前 workload 由 P2 路由决定。
- **没有性能含义**：本报告不包含任何性能结论。

## 6. 下一步

1. 把 cgroup_id/pid 身份接入 BPF class_map 与 per-target 参数。
2. P2：FeatureWindow、规则分类/路由、SLO 门控，最终把 targets 与策略选择连起来。
