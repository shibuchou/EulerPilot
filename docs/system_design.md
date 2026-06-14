# 系统设计图说明

本文档用于说明当前 EulerPilot 在比赛项目中的模块排列方式，以及阅读系统设计图时的顺序。

## 阅读顺序

建议按以下顺序理解系统：

1. `Workloads / Test Targets`
2. `Kernel`
3. `eBPF Observation Layer`
4. `User-space Agent Runtime`
5. `Workload Analysis`
6. `Policy Decision`
7. `Skill Manager`
8. `Execution Layer`
9. `Benchmark / Report / Demo`

## 当前核心主线

```text
观测 workload
  -> 分类 workload
  -> 选择 profile
  -> 通过 cgroup/scx 执行
  -> 用 benchmark 验证
```

## 当前实现状态

- 用户态 Agent 已可编译运行
- `workload_observer.bpf.c` 第一版已落地
- `cgroup_control_skill` 已成为当前最现实、最稳定的主执行路径
- `scx_eulerpilot` 仍为预留实现

## 对应源文件

- `docs/system_design.mmd`
