# OLK-6.6 `sched_ext` 验证记录

验证日期：`2026-06-10`

验证机器：

```text
host: 192.168.1.122
hostname: cernet2.net
os: openEuler 24.03 (LTS-SP3)
project path: /root/EulerPilot
kernel source: /root/olk/kernel-OLK-6.6-atomgit
kernel branch: OLK-6.6
```

## 本次验证目标

本阶段目标不是跑通完整 `scx` 示例，而是先确认独立验证环境已经具备 `sched_ext` 基础能力：

- 能编译并安装 `OLK-6.6` x86 内核
- 能切换并启动到新内核
- 能确认 `CONFIG_SCHED_CLASS_EXT=y`
- 能确认 `/sys/kernel/sched_ext` 存在
- 能确认 `PSI`、`cgroup v2`、BPF 基础能力未失效

## 关键实施过程

### 1. 基础环境

已在该验证机上确认或配置：

- 启动参数包含：
  - `psi=1`
  - `systemd.unified_cgroup_hierarchy=1`
  - `cgroup_no_v1=all`
- `clang`、`gcc/g++`、`make`、`git`、`bpftool`、`pahole`、`libbpf-devel` 等工具链可用
- `PSI` 可读
- unified `cgroup v2` 已挂载

### 2. 源码与配置

采用源码：

- 仓库：`https://atomgit.com/openeuler/kernel.git`
- 分支：`OLK-6.6`

已确认源码中存在：

- `kernel/sched/ext.c`
- `kernel/sched/ext.h`
- `kernel/sched/ext_idle.c`
- `kernel/sched/ext_idle.h`

配置方式：

- 基于 `arch/x86/configs/openeuler_defconfig`
- 显式开启 `CONFIG_SCHED_CLASS_EXT=y`
- 保留 `BPF`、`BTF`、`CGROUPS` 等相关能力

### 3. 编译与安装

本次采用本机 x86 原生编译：

```bash
make openeuler_defconfig
./scripts/config --enable CONFIG_SCHED_CLASS_EXT
make olddefconfig
make -j8 LOCALVERSION=-olk66-scx
make modules_install LOCALVERSION=-olk66-scx
make install LOCALVERSION=-olk66-scx
```

安装完成后已生成新引导项：

```text
openEuler (6.6.0-olk66-scx) 24.03 (LTS-SP3)
```

并已设置为默认启动项后重启。

## 启动后验收结果

重启后实际验收结果如下：

- `uname -r`
  - `6.6.0-olk66-scx`
- `CONFIG_SCHED_CLASS_EXT`
  - `CONFIG_SCHED_CLASS_EXT=y`
- `sched_ext sysfs`
  - `/sys/kernel/sched_ext` 存在
- `PSI`
  - `/proc/pressure/cpu` 可读
- `cgroup v2`
  - `/sys/fs/cgroup` 为 unified `cgroup2`
- `BPF`
  - `bpftool feature probe` 正常输出

本阶段判定：

```text
OLK-6.6 环境部署成功
```

## 当前结论

当前可以明确区分两条路线：

### 主交付路线

- 仍以 openEuler 24.03 LTS SP3 官方内核
- `eBPF + Agent + cgroup v2 + Redis/Nginx 实验 + 中文报告`

### 调度增强验证路线

- 独立验证机上已成功进入 `OLK-6.6` 内核
- `sched_ext` 基础环境已具备
- 后续可以在该机继续做：
  - 最小 `scx` 示例验证
  - `ScxExecutor` 原型接入
  - 再向 `openEuler 24.03-LTS-SP4` 迁移

## 当前尚未完成

在环境验证完成后，已进一步完成一次最小示例短时试跑：

- 示例：`tools/sched_ext/build/bin/scx_simple`
- 试跑方式：短时 `timeout` 启动
- 运行期间：
  - `/sys/kernel/sched_ext/state = enabled`
  - `enable_seq = 1`
  - `nr_rejected = 0`
- 退出后：
  - `/sys/kernel/sched_ext/state = disabled`
  - 日志显示正常从用户态注销退出

这说明当前不仅“内核支持已存在”，而且“最小 `scx` 示例已能被实际加载与退出”。

在此基础上，`2026-06-11` 又继续推进到下一步：

- 项目本地已补出 `scx_eulerpilot.bpf.c` 和 `scx_eulerpilot.c`
- Agent 已补出 pinned `class_map` 写入逻辑
- 当前目标已从“只验证最小 `scx` 示例”推进到“让 workload class 进入 sched_ext 数据面”
- 已在 `OLK-6.6` 机器上验证：
  - `scx_eulerpilot` 可编译、可加载、可退出
  - `/sys/fs/bpf/class_map` 可自动 pin 出来
  - Agent 可把 `redis-server -> 1`、`stress-ng -> 3` 写入 `class_map`
  - `--status / --stats / --detach` 已可用
  - 强制结束 loader 后，`sched_ext` 可回退到 `disabled`

当前仍未完成的内容包括：

- `ScxExecutor` 已接入 EulerPilot 主 Agent，但仍处于 smoke 和参数收敛阶段
- 还未把 `OLK-6.6` 结果写入正式比赛正文结论

因此当前更准确的表述应为：

> EulerPilot 已在独立 `OLK-6.6` 环境上完成 `sched_ext` 基础能力验证，跑通了最小 `scx` 示例，并已验证 `class_map -> scx_eulerpilot` 的第一版类映射链路，但当前正式主交付仍以 SP3 官方内核上的 `cgroup v2` 主执行路径为主。
