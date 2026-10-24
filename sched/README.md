# sched/ — GPL-2.0 调度数据面组件

本目录是从 SchedPilot `0aeca0c` 迁移的 sched_ext 数据面与 loader。**这些文件是 GPL-2.0**，
与 v2 其他目录的许可分开记录；完整许可文本见 `LICENSE.GPL-2.0`。

## 来源与哈希

| 文件 | 来源 | 原 SHA256 | 当前 SHA256 | 修改 |
|---|---|---|---|---|
| `scx_schedpilot.bpf.c` | SchedPilot `0aeca0c` `bpf/scx_schedpilot.bpf.c` | `3d057e6f…` | `cbc3471d…` | struct_ops 名改为 `eulerpilot`；v2 公平性预算；2026-10-22 真实运行时长记账；2026-10-23 绑定任务直投本地 DSQ + 非唤醒 enqueue 唤醒 idle CPU（runnable task stall 修复） |
| `intf.h` | SchedPilot `0aeca0c` `bpf/intf.h` | `c72e7eec…` | `4cfe88cc…` | cfg v3 预算字段；新增 `enq_kick`/`enq_pinned_local` 计数 |
| `loader.c` | SchedPilot `0aeca0c` `loader/scx_schedpilot.c` | `0fe906c7…` | `381aee8c…` | 见下 |
| `scx_schedpilot.bpf.skel.h` | 由 `scripts/build_bpf_sp3.sh` 在 SP3 构建后 `bpftool gen skeleton` 生成 | - | `d1c4e598…` | 生成物（2026-10-23 重新生成；object `eb53ca5d…`） |
| `schedctl.cpp` | 新增（EulerPilot v2），包含 GPL-2.0 `intf.h` | - | `7b030a53…` | cfg/class_map 控制工具 |
| `scx/*.h` | SP3 内核树 `tools/sched_ext/include/scx/`（上游 scx，GPL-2.0） | - | 见各文件 | 无 |

BPF 源的修改：仅 struct_ops 的 `.name` 从 `"schedpilot"` 改为 `"eulerpilot"`；
符号与文件名保持 `schedpilot_*`/`scx_schedpilot`，以免重建目标改名。

`loader.c` 的修改：

1. 默认 pin 命名空间从 `/sys/fs/bpf/schedpilot/v1` 改为 `/sys/fs/bpf/eulerpilot-v2/v1`；
2. 新增 `--pin-dir DIR`；
3. `ensure_pin_dir()` 改为通用 `mkdir_p()`，支持任意 pin 路径；
4. pidfile 从 `/run|/tmp/schedpilot` 改为 `/run|/tmp/eulerpilot-v2`；
5. 新增 `--fd-socket PATH`：attach 后把 struct_ops link FD 经 SCM_RIGHTS 交给执行服务并退出；
6. 帮助文本与 provenance 注释更新。

**v2 修改**：新增 cfg v3 预算字段、统一 `effective_policy`、per-CPU 运行时间预算/aging/撤销路径；
2026-10-22 再给 `sp_task_ctx` 增加 `run_ns`，并把 `stopping()` 的 `consumed` 改为真实占用墙钟时长
（超过 `SP_MAX_RUN_DELAY_NS` 时回退 slice 差值）——内核只在 tick 边界检查 slice，用 `slice - p->scx.slice`
会系统性低估占用，使 `lat_budget_pct` 的封顶判定够不到阈值。DSQ id 与 BPF 函数名不变。struct_ops 已注册为 `eulerpilot`，
`SchedSkill` 以 ops `eulerpilot` 识别调度器。

**预算语义边界（SP3 实测）**：LAT 任务独占 `SP_DSQ_LAT`，被 `scx_bpf_dsq_move_to_local()` 搬走后该 DSQ 即空，
下一次 dispatch 必然转去服务 `SP_DSQ_COMP`（COMP/NORMAL/BG 共用），因此片数近似 1:1 交替、
**时间份额由片长比决定**。`lat_budget_pct` 只能在这个比值之上封顶，本身不产生保底；
要让保留份额生效，执行侧需按 `lat_slice = comp_slice × pct / (100 − pct)` 把预算表达为片长配额
（60% 预算、comp 4ms → lat 6ms）。实测数据与三臂对照见 `evidence/p1/fairness-budget-20261022.txt`。

## 构建

CMake 在找到 libbpf、elf、z 且本目录存在 skeleton 时构建 `ep-sched-loader`；
否则跳过该目标（普通开发机只跑 mock/fake-loader 测试）。

**libbpf 版本要求**：必须使用内核树 `tools/lib/bpf` 构建的静态 libbpf ≥ 1.3。
SP3 的系统 libbpf 是 1.2.2，加载该 struct_ops 对象会在 `SCX_OPS_LOAD` 附近 SIGSEGV；
用内核树的静态库则正常。配置示例：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug \
  -DEP_LIBBPF_STATIC=/root/p0/scx-build/build/obj/libbpf/libbpf.a
```

静态库由 SchedPilot `scripts/build.sh` 或内核树 `tools/lib/bpf` 构建；v2 目前不自动构建它，
这是交付前需要补的可复现步骤。

重新生成 BPF 对象与 skeleton：用 `scripts/build_bpf_sp3.sh`（2026-10-23 起脚本化，
之前这段流程只以散文形式存在，实际无法按需复现——本轮修 runnable task stall 时必须重建
BPF 才暴露出来）。

```bash
bash scripts/build_bpf_sp3.sh /root/epv2-src /root/epv2-src/sched
# 可覆盖：KERNEL_SRC / VMLINUX_DIR / BPFTOOL / WORK
# 需要 clang（本机 17.0.6）、bpftool >= 7.3（struct_ops skeleton）、SP3 内核树头文件
# 输出的 object 与 skeleton sha256 要写进 PROVENANCE.md
```

上游 scx 头文件在本机不是 warning-clean（`scx/common.bpf.h:87` 的
`struct scx_event_stats` 可见性告警），脚本用 `-Wall` 但不加 `-Werror`。

## 使用

```bash
ep-executord --skill sched \
  --loader /path/to/ep-sched-loader \
  --sched-ctl /path/to/ep-schedctl \
  --state-dir /run/eulerpilot-v2/executor
```

`--skill sched` 使用 FD 传递：loader 一次性 attach，随后把 link FD 交给执行服务并退出；
执行服务持有 link FD，崩溃时 FD 关闭即 detach。`--skill sched-ref` 保留原来的子进程模式
（loader 常驻 + PDEATHSIG），用于对照与 fake-loader 测试。

`SchedSkill` 会追加 `--pin-dir /sys/fs/bpf/eulerpilot-v2/<instance_id>`，并在 attach 前检查该目录为空。

## FD 传递

- `--fd-socket PATH` 时 loader 在 attach + pin 后连接 `PATH`，用 `SCM_RIGHTS` 发送
  `bpf_link` FD，收到 ack 后 `_exit(0)`；不销毁 link，执行服务的副本保持调度器 attach。
- 执行服务在 `apply` 前创建 Unix listener，收到 FD 后关闭 listener；`rollback` 关闭 FD 即 detach。
- 崩溃时进程退出关闭 FD，内核 detach；pin 残留由下次启动的 `recover_orphan` 清理。

## ep-schedctl

短命 helper，由执行服务在 attach 后调用；不是长期第二写者。

```bash
ep-schedctl get     --pin-dir DIR
ep-schedctl set     --pin-dir DIR --json '{"lat_slice_ns":2000000,...}'
ep-schedctl restore --pin-dir DIR --json '<get 的完整输出>'
```

- `set` 只接受策略允许的字段：四个 slice、preempt、bg_vtime、llc_affinity；
  每次写入 `cfg.generation += 1`，并回读校验。
- `restore` 使用 apply 前的 cfg 快照恢复参数/flags/mode；generation 继续递增，
  heartbeat 保持当前值。
- `llc_affinity` 通过 `SP_FLAG_LLC_AFFINITY` 位设置/清除。
- 参数范围与策略校验一致（slice 250µs–10ms、preempt 100µs–2ms、bg_vtime 50–400）。
- `class-set --pid N --class BG --confidence 1000` 写入 `class_map`（source=EXTERN，
  updated_ns=CLOCK_MONOTONIC）；`class-get --pid N` 读回。执行服务每 2s 刷新显式
  BACKGROUND pid，并扫描 BACKGROUND cgroup 的 `cgroup.procs`（≤256）后逐 pid 写入；
  条目在 BPF 侧 5s TTL 内保持有效。

## 边界

- `sched` 模式执行服务持有 link FD（FD 传递），loader 一次性 attach 后退出；
  `sched-ref` 仍是子进程 loader，不是执行服务持有的 link FD；
- pin 命名空间是每实例 `/sys/fs/bpf/eulerpilot-v2/<instance_id>/`；
- 参数通过 `ep-schedctl` 短命 helper 写入 cfg map；执行服务不直接持有 map FD；
- 这些文件是 GPL-2.0；v2 其他目录也按 GPL-2.0 发布（见根 `LICENSE`/`NOTICE`）。
