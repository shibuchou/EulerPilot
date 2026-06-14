# PSI 复用设计

## 目标

本文档说明如何将 Android `PerfInsight` 原型中的 PSI 相关设计和源码，按 openEuler / EulerPilot 的边界进行复用。

这里最关键的判断是：

- 不是整体复用 Android 工程
- 最有价值的不是单独的 `psi_pf.*`
- 真正值得复用的是 **PSI 感知的门控思想**
- 相关核心逻辑不仅在 `psi_pf.*`，也在 `merge_latency_lessfor.*`

## 参考来源

当前参考源码快照位于：

```text
/root/EulerPilot/third_party/reference/perfinsight-psi/
```

重点文件：

```text
psi_pf.bpf.c
psi_pf.c
merge_latency_lessfor.bpf.c
merge_latency_lessfor.c
perfinsight.h
tags1.yaml
```

## 哪些部分可以复用

### 1. PSI 观测逻辑

`psi_pf.bpf.c` 与 `psi_pf.c` 展示了：

- 子系统分类：`IO / MEM / CPU`
- 指标分类：`SOME / FULL`
- 时间窗口：`avg10 / avg60 / avg300`
- `total`
- 固定点 PSI 解码方式

这些内容可以直接作为 EulerPilot 中 PSI 数据结构和格式化逻辑的参考。

### 2. PSI 门控逻辑

这部分是最有价值的复用目标。

关键概念包括：

- `threshold`
- `trigger_resource`
- `trigger_window`
- `trigger_index`
- `psi_handle_flag`
- `trigger_flag`
- `should_trigger(...)`

它把 PSI 从一个“被动指标”变成了一个“主动门控信号”：

```text
低压力：
  抑制重型采集与控制

高压力：
  进入更强的观测与控制窗口
```

这和 EulerPilot 现在的需求非常吻合。

### 3. 多源证据组织方式

Android 原型不是把 PSI 当作最终结论，而是把它当作：

```text
是否进入有效压力窗口
```

然后再结合更丰富的 eBPF 证据做分析。

EulerPilot 可以直接复用这个思路：

```text
PSI 压力
  -> 调度证据
  -> workload 分类
  -> cgroup/scx 动作
  -> benchmark 结果
```

### 4. 实验元数据与报告流程

PerfInsight 中“结果目录 + 元数据 + CSV + 自动报告”的组织方式，也值得在 EulerPilot 中复用。

## 哪些部分不能直接复用

### 1. Android 部署与工具链

这些内容都不应直接搬进 EulerPilot：

- Android NDK
- ARM64 Android 交叉编译默认配置
- ADB 部署流程
- `/system/etc/bpf`
- Android GKI 假设
- `dumpsys gfxinfo`
- Jank 标签

### 2. Android 侧异常标签

PerfInsight 主要面向：

- Jank
- frame delay
- Android responsiveness

EulerPilot 当前目标是：

- Redis P99/P999
- Nginx latency
- sysbench throughput
- mixed workload interference

所以只能复用“压力窗口 + 证据保留”的思想，不能直接复用 Android 标签体系。

### 3. Android 工程目录结构

不建议把 Android 工程整套结构直接复制到 EulerPilot 核心目录。

正确做法是：

- 原始源码放在 `third_party/reference/perfinsight-psi/` 作为 reference
- 在 EulerPilot 内按当前 Linux/openEuler 环境重新实现

## 建议的并入方式

### 第一层：先做用户态 PSI reader

建议优先实现：

```text
agent/observer/psi_reader.cpp
agent/observer/psi_reader.h
```

读取：

- `/proc/pressure/cpu`
- `/proc/pressure/memory`
- `/proc/pressure/io`

这是当前最稳、最可移植、最适合 openEuler 的第一步。

### 第二层：再做 EulerPilot 本地 PSI gate

后续再把 Android 里的 PSI 门控思想抽象出来：

```text
bpf/psi_gate.bpf.c
agent/observer/psi_gate.cpp
agent/include/psi_gate.hpp
```

但这里应当是：

- 参考 Android 原始逻辑
- 按 EulerPilot 的构建链、代码边界、部署方式重写

而不是直接搬源码。

## 在 EulerPilot 中怎么用 PSI

### 1. 作为策略触发信号

不建议写成“完全严格的全 AND”，也不建议写成简单 OR。

当前更合理的策略是：

```text
基础前提用 AND
压力证据用 OR / 打分
控制强度分级
```

也就是：

### 第一层：是否进入资源管控场景

```text
if latency_workload_exists
and background_workload_exists:
    enter interference_candidate_state
```

这两个是硬条件。

### 第二层：是否进入轻度控制

```text
if cpu_psi_high
or latency_workload_wait_high
or background_runtime_high:
    switch latency_profile
```

也就是：

- 只要出现一种明确压力证据，就可以进入轻度控制
- 轻度控制主要做：
  - 提高 latency workload 权重
  - 适度压低 background 权重

### 第三层：是否进入强控制 mixed_profile

```text
if cpu_psi_high
and latency_workload_wait_high:
    switch mixed_profile
```

这时说明：

- 系统整体 PSI 压力升高
- latency workload 自身调度等待也同步升高

此时才进入更强控制。

### 最终推荐的触发逻辑

```text
if latency_workload_exists and background_workload_exists:

    if cpu_psi_high and latency_workload_wait_high:
        switch mixed_profile
        apply strong background control

    elif cpu_psi_high or latency_workload_wait_high or background_runtime_high:
        switch latency_profile
        apply light background control

    else:
        keep normal_profile or observe only
```

这个逻辑比简单 AND 或简单 OR 更适合真正的系统 Agent。

## PSI 在 EulerPilot 中承担的角色

### 1. PSI 作为策略触发信号

PSI 不直接等同于 Redis 性能变差，而是用于标记系统是否进入值得控制的压力窗口。

### 2. PSI 作为报告证据

在 benchmark 报告中加入：

- baseline CPU PSI
- noisy CPU PSI
- controlled CPU PSI

帮助解释：

- Agent 为什么出手
- 系统是否真的处于压力状态
- 控制动作是否改变了压力水平

### 3. PSI 作为控制循环状态

未来短时控制循环可写成：

```text
每秒：
  读 PSI
  读 eBPF maps
  更新 workload scores
  判断是否进入压力窗口
  决定 profile
  执行 cgroup/scx 动作
```

## 复用边界总结

### 保留为 reference

- 原始 `psi_pf.bpf.c`
- 原始 `psi_pf.c`
- 原始 `merge_latency_lessfor.bpf.c`
- 原始 `merge_latency_lessfor.c`
- 原始 `perfinsight.h`
- 原始 YAML 示例

### 在 EulerPilot 中重写实现

- PSI reader
- PSI gate abstraction
- profile trigger logic
- cgroup pressure-aware policy
- report integration

## 当前建议

当前最推荐的推进顺序是：

1. 先实现 `psi_reader.cpp/.h`
2. 把 `/proc/pressure/*` 接进 Agent 主循环和实验报告
3. 再实现 EulerPilot 本地 `psi_gate`
4. 最后再决定是否值得做 BPF 版 PSI gate
