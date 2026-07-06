# scx_eulerpilot

本目录用于实现 sched_ext/scx 调度器预留后端。

目标设计：

- latency_dsq：延迟敏感任务
- batch_dsq：吞吐型任务
- background_dsq：后台干扰任务
- class_map：由用户态 Agent 更新，scx 调度器读取
- mixed_profile：前台低延迟 + 后台不饿死

当前 openEuler 24.03 LTS SP3 官方内核未包含可直接启用的 `sched_ext/scx` 实现，因此：

- 第一阶段主执行路径为 `cgroup v2`
- 本目录保留 ScxExecutor 的接口和实现位置

截至 `2026-07-06`，openEuler 24.03 LTS SP4 验证机已完成启用
`CONFIG_SCHED_CLASS_EXT=y` 的自编译内核验证，`scx_eulerpilot` 可通过：

```bash
scripts/build_scx_eulerpilot.sh
```

构建并安装到 `/usr/local/bin/scx_eulerpilot`。脚本默认使用
`/root/kernel-build/src-scx/tools/sched_ext`，可通过 `KERNEL_SRC`、
`SCHED_EXT_DIR`、`SCX_BUILD_DIR` 和 `INSTALL_BIN` 覆盖。

截至 `2026-06-11`，独立 `OLK-6.6` 验证线已经推进到第一版类映射原型：

- 用户态 Agent 已支持 `--backend sched_ext`
- Agent 已能把 workload 分类结果写入 pinned `class_map`
- Agent 侧执行逻辑已从 `runtime.cpp` 中收口到独立 `ScxExecutor` 模块
- `scx_eulerpilot.bpf.c` 已补出第一版多 DSQ 原型：
  - `latency`
  - `batch`
  - `background`
  - `shared`
- `scx_eulerpilot.c` 已补出第一版用户态加载器
- 已补第一版统计导出：
  - class 命中计数
  - dispatch 命中计数
- 已补第一版工程管理能力：
  - `--status`
  - `--stats`
  - `--detach`
  - kill-loader 后自动回退

当前这版还不是最终的 `scx_eulerpilot`：

- 还没有完整的性能调优
- 还没有复杂的 per-task 动态反馈
- 还没有进入正式比赛主实验

因此它的定位是：

- 先把 `sched_ext` 从“全局启停”推进到“类级 DSQ 原型”
- 先验证 Agent 分类结果能够进入 scx 侧数据面
- 再继续演进到真正可用于实验对比的 `scx_eulerpilot`

## 当前文件

- [scx_eulerpilot.bpf.c](/root/EulerPilot/sched/scx_eulerpilot.bpf.c:1)
- [scx_eulerpilot.c](/root/EulerPilot/sched/scx_eulerpilot.c:1)

## 当前约定

- `class_map` key：`tgid`
- `class_map` value：
  - `0`：normal
  - `1`：latency
  - `2`：batch
  - `3`：background

`scx_eulerpilot` 会保留 libbpf 默认 pinned map，并额外 pin 到 EulerPilot
命名空间，供 Agent 与 `psi_gate` 稳定发现：

```text
/sys/fs/bpf/eulerpilot/scx_eulerpilot/v1/class_map
/sys/fs/bpf/eulerpilot/scx_eulerpilot/v1/gate_state_map
/sys/fs/bpf/eulerpilot/scx_eulerpilot/v1/stats
```

Agent 在 `sched_ext` 后端启用后，会优先尝试打开命名空间 pinned
`class_map`，并把识别结果同步进去。
