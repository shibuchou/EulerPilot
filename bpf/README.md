# eBPF 观测模块

本目录用于实现 EulerPilot 的 workload 行为观测程序。

## 当前状态

- 第一版 CO-RE observer 已可构建并运行。
- 已验证 `tp_btf/sched_wakeup`、`tp_btf/sched_switch`、`tp_btf/sched_migrate_task` 可在当前 openEuler 24.03 LTS SP3 内核上加载。
- 当前通过 `task_metrics_map` 按 PID 聚合观测数据，并由 `build/workload_observer_dump` 或主 Agent 读取输出。
- `network_policy_demo.bpf.c` 提供 `cgroup/connect4` 子能力，当前用于正式 `network_policy` 的端口拒绝与统计。
- `network_qos_tc.bpf.c` 提供 `tc_egress` classifier 子能力，当前用于 `network_qos` 的 lab veth QoS 命中统计，限速由 TBF qdisc 执行。
- `network_xdp_demo.bpf.c` 提供 `xdp` 子能力，当前用于 `network_xdp` 在 isolated veth 上执行 ICMP drop/pass 和命中统计；默认不挂真实业务网卡。

## 当前已采集的证据

- `wakeup_count`
- `total_wait_ns`
- `runtime_ns`
- `ctx_switch_count`
- `migrate_count`
- `last_cpu`

这些证据主要用于：

- 识别延迟敏感 workload
- 识别后台干扰 workload
- 生成调度等待和运行时行为证据

## 第一版目标

- 调度事件采集：
  - `sched_wakeup`
  - `sched_switch`
  - `sched_migrate_task`
- task 级等待/运行/唤醒/迁核统计
- map dump 输出给用户态 Agent

## 快速验证

```bash
make observer
timeout 5s ./build/workload_observer_dump
```

工程底座优先参考 `libbpf-bootstrap`。

Network 子能力快速构建：

```bash
make network-policy-demo network-qos-tc network-xdp-demo
```
