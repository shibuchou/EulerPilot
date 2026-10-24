# P0 环境与参考数据面复验报告（SP3）

日期：2026-10-21；状态：**P0 完成（有明确限制）**。本文只记录实测；A/B/C 数字是 smoke，不是性能结论。
证据：`evidence/p0/sp3-environment-20261021.json`、`evidence/p0/sp3-reference-manifest.json`、
`evidence/p0/abc-smoke-20261021/`。

## 1. 结论摘要

openEuler 24.03-LTS-SP3 虚拟机已恢复并复用，运行自编译 `6.6.0-schedpilot-sp3`。
在调整内核启动参数后，**cgroup v2 与 PSI 均可用**。从 SchedPilot `0aeca0c` 干净重建了
loader / daemon / BPF 对象，并完成 quiet 与 noisy 两组 A/B/C 最小实验（各 3 轮 × 15s，
0 无效轮）。三臂均能正确切换、负载存活、结束后回到 fair。

**这些数字只证明链路可运行，不构成性能结论**：n=3、单 VM、宿主还有其它 VM
（检查时 load average ≈ 12），且缺少 perf 计数。正式结论必须按实验协议重跑。

## 2. 环境与访问

| 项 | 值 |
|---|---|
| 宿主 | `<JUMP-HOST>`（Ubuntu 24.04，48 CPU / 125 GiB，检查时可用约 31 GiB） |
| VM | libvirt `schedpilot-sp3`：8 vCPU / 16 GiB / 桥接 `phybr0` |
| 客户机 | `schedpilot-sp3`，openEuler 24.03 (LTS-SP3)，`6.6.0-schedpilot-sp3` |
| 地址 | `<SP3-IP>`（DHCP） |
| 访问 | `ssh -i <SSH-KEY> -J <JUMP-HOST> root@<SP3-IP>` |
| SP4 | `schedpilot-sp4` 已启动，`<SP4-IP>`，SP4 + `6.6.0-schedpilot`；已完成一次 attach/detach sanity（loader `76f0cad3…`），未做实验 |

两台 VM 原先均关机。`~/.ssh/config` 里的网关端口 2226/2243 当前不可用，改用 `-J <JUMP-HOST>`。

## 3. 环境改动（可回滚）

发行版 systemd 编译为 `default-hierarchy=legacy`，内核 `CONFIG_PSI_DEFAULT_DISABLED=y`，
默认只有 cgroup v1 且无 `/proc/pressure`。执行：

```bash
grubby --update-kernel=ALL --args="psi=1 systemd.unified_cgroup_hierarchy=1"
reboot
```

备份：`/root/p0-backup/grub.default.20261021-025103`、`grub.cfg.20261021-025103`。
回滚：`grubby --update-kernel=ALL --remove-args="psi=1 systemd.unified_cgroup_hierarchy=1" && reboot`。

重启后 systemd 289 个单元、0 失败；`/sys/fs/cgroup` 为 `cgroup2fs`，
`/proc/pressure/{cpu,io,irq,memory,stat}` 存在。**这使当前环境与 SchedPilot 历史 SP3 证据
（cgroup v1、PSI 关闭）不再完全一致**，引用旧数值时必须说明。

## 4. 参考数据面干净构建

- 源码：本地 `D:\code\Ubuntu\schedpilot` 的 `0aeca0c`，用 `git bundle` 传输并在
  `/root/p0/schedpilot-0aeca0c` 克隆；guest 内 `git status` 干净，234 个跟踪文件。
- 内核树：`/usr/src/linux-6.6.0-145.3.34.165.oe2403sp3.x86_64`；其 `.config`
  与运行内核 `/proc/config.gz` 同为 `c43142c3…`。
- 构建：`scripts/build.sh --kernel-src <上树> --jobs 8`，退出码 0。注入到内核树的三个文件
  与仓库源文件哈希一致；旧副本备份在 `/root/p0/backup-injected`。
- 产物哈希：loader `3da28c29…`、daemon `f1f60b70…`、BPF 对象 `ba1c2a63…`。
- 旧 `/root/schedpilot`（v0.3.1 时代）未覆盖，loader 仍为 `e7ebe09a…`。

## 5. 结果矩阵

层级：环境 / 构建 / 内核集成 smoke / 性能 smoke。未测项不写 PASS。

| 检查 | 层级 | 结果 | 依据 |
|---|---|---|---|
| SP3 发行版 | 环境 | PASS | `os-release` |
| 内核与源码/config 对应 | 构建 | PASS | 源码树 `.config` 与 `/proc/config.gz` 哈希一致 |
| BTF | 环境 | PASS | `/sys/kernel/btf/vmlinux`，59 个模块 BTF |
| cgroup v2 | 环境 | PASS（改启动参数后） | `cgroup2fs`，`/proc/self/cgroup` 为 `0::/…` |
| PSI | 环境 | PASS（改启动参数后） | `/proc/pressure/{cpu,io,irq,memory}` 可读 |
| 参考代码干净重建 | 构建 | PASS | `BUILD_EXIT=0`；产物哈希见 §4 |
| attach / detach | 内核集成 smoke | PASS | 6 轮 B/C 均 `state=enabled ops=schedpilot`，结束回 `disabled`；dmesg 记录 enabled/disabled |
| attach 下 workload 存活 | 内核集成 smoke | PASS | quiet 与 noisy 的 Redis 压测均完成，0 无效轮 |
| PMU cycles/instructions | 内核集成 smoke | PASS | 直接 `perf_event_open`：cycles=1.12e9、instructions=9.02e8，`enabled==running` |
| PMU cache-misses 语义 | 内核集成 smoke | PARTIAL | 1.5 亿次循环仅 4366 次，不能当 LLC misses |
| LLC 软亲和 | 环境 | UNSUPPORTED | 每 vCPU 独立 16 MiB L3；无真实共享 LLC |
| perf 计数 | 性能 smoke | UNSUPPORTED | 无 `perf` 二进制，`perf_stat.txt` 未生成，csw/migrations/ipc/mpki 为空 |
| 工具链 | 环境 | PARTIAL | clang 17.0.6、gcc 12.3.1、bpftool 7.2.0、pahole 1.28、libbpf 1.2.2；cmake/ninja/perf 缺失 |

`env_check.json` 的 `WARN pmu "no hardware PMU counters"` 是**假阴性**：它调用的是
`perf stat`，而 `perf` 未安装；同一台机器上直接 `perf_event_open` 读取 cycles/instructions
是成功的。daemon 本身用 `perf_event_open`，不依赖 `perf` 二进制。

## 6. A/B/C 最小实验（smoke，不是性能结论）

统一条件：Redis GET，A=fair、B=basic、C=class，server CPU `0-3`，client CPU `4-7`，
每臂 3 轮、每轮 15s、预热 5s，0 无效轮。宿主同时运行其它 VM（load ≈ 12）。

**quiet（无人工干扰）**

| arm | n | QPS 中位 [IQR] | 相对 A | p99 中位 (ms) |
|---|---:|---:|---:|---:|
| A | 3 | 47,141 [4,556] | +0.0% | 1.607 |
| B | 3 | 61,635 [8,072] | +30.7% | 1.079 |
| C | 3 | 61,462 [4,091] | +30.4% | 1.143 |

配对复算：B vs A QPS +33.23%（95% CI [+15.54%, +50.92%]，3/3 更高），p99 −34.97%；
C vs B 的 QPS 差 −0.3%（分类在 quiet 下没有可辨识增益）。

**noisy（stress-ng CPU+VM 干扰与服务共享 0-3）**

| arm | n | QPS 中位 [IQR] | 相对 A | p99 中位 (ms) |
|---|---:|---:|---:|---:|
| A | 3 | 17,447 [8,511] | +0.0% | 4.559 |
| B | 3 | 21,166 [8,662] | +21.3% | 5.111 |
| C | 3 | 39,658 [3,452] | +127.3% | 3.287 |

配对复算：B vs A QPS +19.71%（95% CI [−3.13%, +42.54%]，跨零）；C vs B 中位 +87.4%。

**解释边界**：A 臂 noisy IQR 达中位数的 49%，说明这台共享 VM 的噪声极大；n=3、15s
不足以支持任何方向性结论。quiet 下 B 的 +30% 也可能包含宿主负载/缓存状态差异。
本表只用于确认三臂可运行、切换有效、没有失速；正式实验必须按 20 block、60s、
固定种子和预注册阈值重跑。

BG 进度：参考脚本的干扰负载不导出工作单元，**本次未测 BG 吞吐**；这正是设计文档要求
v2 自己实现“压力生成器导出真实进度”的原因，不能用这次 smoke 冒充公平性结论。

## 7. 证据与 manifest

- `evidence/p0/sp3-environment-20261021.json`：环境、哈希、逐项检查。
- `evidence/p0/sp3-reference-manifest.json`：源码 commit/bundle、内核、工具链、构建命令、
  注入文件、产物哈希、实验 ID、限制。
- `evidence/p0/abc-smoke-20261021/`：两组实验的 `summary.md`、`per_run.csv`、
  `experiment.meta.json`、每轮 `summary.json` 与调度器状态快照；
  原始归档 `/root/p0/p0-abc-evidence.tar.gz`，sha256 `02e27ff7…`。
- 完整原始数据仍在 SP3：`/root/p0/results/abc-{quiet,noisy}-*`。

## 8. 限制与未覆盖

1. **perf 缺失**：没有每轮 csw/migrations/IPC/MPKI；PMU 分类只能依赖 daemon 自己的计数。
   后续（P1，2026-10-21）：SP3 已安装 `cmake 3.27.9` 与 `perf 6.6.0-145.3.35`，
   `env_check` 变为 `ok=32 warn=3 fail=0`，PMU 为 OK。但本次 P0 smoke 的数据没有 perf 计数，
   不能追溯补记。
2. **伪 LLC**：这台 VM 上 LLC 亲和不可验证，只能标 UNSUPPORTED。
3. **共享宿主**：<JUMP-HOST> 上还跑着 k8s/openkylin 等 VM，噪声大；正式性能不能在这台机器上边跑边做别的事。
4. **cgroup 默认 v1**：若评审机器不加启动参数，v2 的 cgroup 路径必须能降级或明确要求该参数。
5. **SP4 未参与 P0 对照**：已启动并完成一次 attach→`stress-ng` 4/4→detach sanity
   （loader `76f0cad3…`，与 SchedPilot formal-5 记录一致），但 SP4 上 cgroup 仍为 v1、PSI 关闭，
   且没有跑任何 A/B/C。SP4 仅作可选兼容验证，不能替代 SP3。
6. **A/B/C 数字不可引用为性能**：样本量、时长、环境都不满足实验协议。

## 9. 下一步（进入 P1）

1. 安装/补齐 `perf`（或确定替代计数方案），确认 daemon PMU 分类在 SP3 上真实可用。
2. 建立 v2 的 ABI 冻结与唯一写者骨架：先把参考 loader 的 pin/所有权行为、双实例拒绝、
   stale pin reconcile 做成可测试项。
3. 迁移静态数据面时先修公平性与心跳回退，再谈自适应；用本报告的 quiet/noisy 作为
   功能回归基线，而不是性能基线。
4. 在正式性能前，先把实验脚本的 BG 工作单元、perf 计数、无效轮账本补齐。

## 10. 对设计文档的影响

- 方案 §3.1 的 SP3 可运行性成立；但 **cgroup v2 与 PSI 不是发行默认**，部署说明必须覆盖。
- 方案 §6.2 的拓扑退化在 SP3 VM 上确认存在，LLC 亲和默认关闭并记录 `UNSUPPORTED`。
- 旧结果对比必须注明本次环境变更（cgroup v1→v2、PSI 关闭→开启）。
- `env_check` 的 PMU WARN 不能直接当作“无 PMU”，要以实际 `perf_event_open` 为准。
