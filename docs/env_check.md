# 环境检查

检查日期：`2026-06-10`

服务器信息：

```text
host: cernet2.net
os: openEuler 24.03 (LTS-SP3)
kernel: 6.6.0-132.0.0.111.oe2403sp3.x86_64
project path: /root/EulerPilot
```

## 当前已核实事实

- `gcc`、`g++`、`clang`、`make`、`git`、`bpftool`、`llvm-strip`、`readelf` 已安装
- `/sys/kernel/btf/vmlinux` 可用
- `CONFIG_SCHED_CLASS_EXT=y` 未在 `/proc/config.gz` 中检测到
- `/sys/kernel/sched_ext` 不存在
- `CONFIG_PSI=y` 存在，且 PSI 已通过启动参数启用
- `cgroup2` 已以 unified 方式挂载
- 当前内核启动参数已包含：
  - `psi=1`
  - `systemd.unified_cgroup_hierarchy=1`
  - `cgroup_no_v1=all`

## 官方 SP3 主线环境已验证

在更新启动参数并重启后，已确认：

- `/proc/pressure/cpu` 存在且可读
- `/sys/fs/cgroup` 为 `cgroup2`
- `sched_ext/scx` 在当前官方 SP3 内核上仍不能作为正式主执行后端直接使用

## 现阶段意义

当前项目的主要风险已不再是工具链，而是平台能力边界：

- `sched_ext/scx`
- PSI 触发阈值调优
- `cgroup v2` 控制策略效果稳定性

当前阶段的主目标是：

- 用官方 openEuler 24.03 LTS SP3 内核跑通主闭环
- 使用 eBPF 采集 workload 行为
- 使用用户态 Agent 做分类和策略决策
- 使用 `cgroup v2` 作为当前主执行后端
- 产出 benchmark 与中文报告

## 当前下一步

1. 继续稳定 Redis 主实验结果
2. 在 Redis 主实验中验证推荐参数 `1000/5`
3. 持续调优 PSI 阈值，但不让其拖慢主交付
4. `sched_ext/scx` 保持预留，主线仍不依赖它

## 独立 OLK-6.6 验证线最新状态

在独立验证机 `192.168.1.122` 上，已完成：

- `OLK-6.6` 内核源码编译与安装
- 新内核启动：`6.6.0-olk66-scx`
- `CONFIG_SCHED_CLASS_EXT=y` 核验通过
- `/sys/kernel/sched_ext` 已存在
- `PSI`、`cgroup v2`、`bpftool` 基础能力仍正常

这说明：

- 当前官方 SP3 主线仍以 `cgroup v2` 为主执行路径
- 但后续 `sched_ext/scx` 的独立环境已经准备完成
- 后续可以在该验证机继续做最小 `scx` 示例与 `ScxExecutor` 对接
