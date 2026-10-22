# P1 SchedSkill 报告（真实 sched_ext 闭环）

日期：2026-10-21；状态：**v2 自建 loader 已接入；`sched` 模式由 loader 一次性 attach 后通过
SCM_RIGHTS 把 link FD 交给执行服务并退出，`sched-ref` 保留子进程对照。SP3 真实
attach/detach 与崩溃回收通过。**
loader 由 v2 `sched/` 源构建，pin 命名空间为每实例
`/sys/fs/bpf/eulerpilot-v2/<instance_id>`，struct_ops 已注册为 `eulerpilot`；
`sched` 模式下执行服务持有 link FD，崩溃时 FD 关闭即 detach。边界见 §6。

证据：[sp3-sched-integration.txt](../../evidence/p1/sp3-sched-integration.txt)、
[sp3-v2-loader.txt](../../evidence/p1/sp3-v2-loader.txt)、
[sp3-instance-pin.txt](../../evidence/p1/sp3-instance-pin.txt)、
[sp3-cfg-params.txt](../../evidence/p1/sp3-cfg-params.txt)、
[sp3-fd-link.txt](../../evidence/p1/sp3-fd-link.txt)。

## 1. 做了什么

`SchedSkill` 支持两种模式：`sched`（FD 传递，执行服务持有 link FD）与 `sched-ref`
（子进程 loader，用于测试与对照）。

| 阶段 | 行为 |
|---|---|
| probe | 检查 loader 可执行、sched_ext 存在、state=disabled、ops 无 eulerpilot、pin 目录为空、无 runtime 标记、无 `schedpilotd` |
| plan | 只接受 `basic` / `class`；`adaptive` 明确拒绝（需要 schedpilotd，P1 不接） |
| snapshot | 记录 state/ops/enable_seq/pins/runtime 是否存在 |
| apply | `sched`：fork 一次性 loader，接收 link FD（SCM_RIGHTS），loader 退出；`sched-ref`：loader 保持子进程。两者都等待 state=enabled 且 ops=eulerpilot、pin 集合稳定；若有 `spec.parameters`，先 `ep-schedctl get` 快照，再 `set` 写入并回读，最后写 runtime 标记 |
| verify | `sched`：link FD 有效；`sched-ref`：loader pid+starttime 匹配。两者都检查 state=enabled、ops=eulerpilot、enable_seq；有 cfg 快照则重读比对 |
| rollback | 若有 cfg_before，先 restore；`sched` 关闭 link FD，`sched-ref` SIGTERM→SIGKILL；确认 detached；只删除 owned pin；删除 runtime 标记 |
| verify_rolled_back | state=disabled、ops 无 eulerpilot、pin 为空、runtime 标记已删；`sched` 还要求 link FD 已关闭 |
| reconcile / recover_orphan | `sched`：崩溃时 link FD 已关闭，清理残留 pin；`sched-ref`：处理孤儿 loader；有 cfg 快照先 restore |

## 2. 所有权设计

`sched` 模式：loader 一次性 attach 后把 link FD 交给执行服务并退出；执行服务持有 FD，
崩溃时进程退出关闭它，内核 detach，不依赖 PDEATHSIG。下面描述的是 `sched-ref` 子进程对照模式。

执行服务崩溃时不能让 loader 继续跑。子进程在 exec 前设置 `PR_SET_PDEATHSIG=SIGTERM`，
因此 `ep-executord` 被 SIGKILL 后 loader 收到 SIGTERM、退出并 detach，fair 接管。

pin 所有权靠三层信息，而不是“发现旧 pin 就清空”：

1. `sched-runtime.json` 记录 pid、`/proc/<pid>/stat` 的 starttime、mode、enable_seq、每个 pin 的
   name/inode/device；
2. 停止进程前核对 pid + starttime + cmdline，避免 PID 复用误杀；
3. 清 pin 前逐个核对 inode/device；发现任何不属于本次的 pin 就报告冲突，不删除。

启动时 `recover_orphan` 在 journal reconcile 之前运行，处理“已 COMMITTED 但执行服务崩溃”的
孤儿 loader；这是普通未完成事务扫描覆盖不到的场景。

互斥：`preflight` 发现 `schedpilotd` 正在运行会直接拒绝 attach。`sched-ref` 已使用独立的
v2 pin 命名空间，但旧 daemon 仍可能争用同一个 sched_ext 调度器；执行服务的 flock 只约束
v2 实例，不能约束旧 daemon，所以保留显式检查。

## 3. 实测结果（SP3）

环境：openEuler 24.03-LTS-SP3，`6.6.0-schedpilot-sp3`，8 vCPU。
loader：`/root/epv2-build/ep-sched-loader`（由 v2 `sched/` 源构建），sha256 `3aa11a6b…`。
`ep-schedctl` sha256 `9bd709df…`，`ep-executord` sha256 `4077037c…`，`ep` sha256 `6b33142a…`。
loader 链接内核树静态 libbpf 1.3；SP3 系统 libbpf 1.2.2 会在加载 struct_ops 对象时 SIGSEGV，
详见 `sched/README.md` 与 [sp3-v2-loader.txt](../../evidence/p1/sp3-v2-loader.txt)。

`ctest`（SP3）：**6/6 通过**

```text
unit                Passed  1.39s
integration         Passed  2.65s
integration_sched   Passed  2.16s   # sched-ref 子进程模式
integration_fd      Passed  3.41s   # sched FD 传递模式
integration_lease   Passed  1.61s   # 租约过期自动回退
integration_ttl     Passed 11.07s   # 策略 TTL 自动回退
```

`integration_fd` 的断言链（实例 `inst-18f3792b600ad12b`）：

1. apply → `state=enabled`、`ops=eulerpilot`、实例 pin/`cfg` 存在；
2. 执行服务进程持有 `bpf` fd，`ep-sched-loader` 进程已退出（link FD 已交付）；
3. `ep-schedctl get` → cfg generation 2；lat=2ms、cache=4ms、llc=false 与策略一致；
4. rollback → `state=disabled`、pin/runtime 清理；
5. 再 apply → `SIGKILL` 执行服务 → link FD 关闭，1s 内 `state=disabled`，pin 残留；
6. 重启 → `recover_orphan` 清理残留 pin，`degraded=false`，generation 保持 3；
7. 结束后无 pin、无 loader 进程。

`integration_lease` 用 `--ttl-ms 1000` apply，等待租约过期；执行服务的 500ms 监控自动
rollback 到 fair，清 pin，`status.degraded=true`，`last_fallback="lease expired"`。

`integration_ttl` 用 `policy_ttl_ms=10000` 的合法策略 apply，TTL 到期后同一监控自动回退，
`last_fallback="policy TTL expired"`。

该测试在 SP3 上重复运行两次均通过。手动调试时观察到 `enable_seq` 从 15→16（attach），
detach 不改变它——这正是不能用“整快照比对”校验回滚的原因。

## 4. 测试暴露并修复的三个问题

1. **pin 记录竞态**：`state=enabled` 先于 loader pin map 出现，首次实现记录到不完整 pin 集合，
   回滚把后出现的 pin 判成外部所有。修复：等 pin 集合连续 4 次采样稳定后再记录。
2. **回滚校验误用整快照**：通用“再 snapshot 一次并比较”把单调的 `enable_seq` 也算进去，
   回滚成功后仍报 VERIFY_FAILED。修复：Skill 增加 `verify_rolled_back`，按域语义校验；
   mock 比较值，sched 比较 state/ops/pins/runtime。
3. **重启测试竞态**：SIGKILL 后旧 socket 文件仍在，测试把“文件存在”当作“服务就绪”。
   修复：以一次成功的 `ep status` 作为就绪条件，并在启动前删除旧 socket。

## 5. 对 ABI 的影响

新增两个 Skill 生命周期方法（已写入 `ABI_FREEZE.md`）：

- `verify_rolled_back(snapshot, deadline)`：回滚后的域语义校验；
- `recover_orphan(error)`：执行服务启动时的非事务资源回收。

`ep-executord` 新增 `--loader PATH` 与 `--sched-ctl PATH`；`--skill sched` 选择 FD 传递模式，
`--skill sched-ref` 选择子进程对照模式，`--skill mock` 仍是测试用。`spec.parameters` 由短命
`ep-schedctl` 写入 cfg map，回读校验，rollback/recover 先恢复快照再 detach。协议与 envelope 未变。

## 6. 明确边界

- **link FD 由执行服务持有**：`sched` 模式下 loader 一次性 attach 后通过 SCM_RIGHTS 交付
  link FD 并退出；执行服务崩溃时 FD 关闭即 detach。`sched-ref` 仍保留子进程模式用于对照。
- **pin 已按实例隔离**：`/sys/fs/bpf/eulerpilot-v2/<instance_id>`。
- **BPF 对象只改了 ops 名**：struct_ops 现注册为 `eulerpilot`，DSQ/分类逻辑仍继承 SchedPilot；
  cfg 参数由短命 `ep-schedctl` helper 写入，执行服务不直接持有 cfg map FD。
- **只支持 basic/class**：adaptive 需要 schedpilotd，被明确拒绝。
- **libbpf 版本敏感**：必须用内核树静态 libbpf ≥1.3；系统 1.2.2 会崩溃。这是部署约束。
- **策略边界另见** [POLICY_BINDING_REPORT.md](POLICY_BINDING_REPORT.md)；本报告不重复。
- **不证明性能**：本报告没有任何性能结论。

## 7. 下一步

1. TargetBinding 的 cgroup_id/pid 身份接入 class map 的实例键，并补 fail-safe 测试。
2. 再进入 P2 的规则路由与 SLO 门控。
3. 可选：把 loader 的一次性 attach 也收进执行服务进程，消除 GPL 组件的单独二进制；
   当前 FD 传递已经满足“执行服务持有 link FD”的所有权要求。
