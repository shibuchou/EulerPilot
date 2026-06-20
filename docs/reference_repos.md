# EulerPilot 参考仓库说明

更新时间：`2026-06-19`

本文档只描述“已经确认对 EulerPilot 有价值的参考代码”，不把这些参考仓库等同于生产代码。

## 1. libbpf-bootstrap

来源：

- `D:\code\Ubuntu\libbpf-bootstrap`

当前用途：

- 作为 eBPF 程序最基础的构建、skeleton 和用户态装载参考
- 继续支撑 `Observer`、`sched_ext` 相关最小程序组织方式

## 2. PerfInsight PSI

来源：

- `third_party/reference/perfinsight-psi`

当前用途：

- 作为 `PSI` 读取与压力门控逻辑的参考快照
- 重点帮助 `PsiGate` 设计，而不是直接复制成生产模块

## 3. lmp-xdp-lsm

来源：

- `D:\code\Ubuntu\lmp`
- 项目快照位置：`third_party/reference/lmp-xdp-lsm`

当前用途：

- 为后续 `NetworkPolicySkill` 提供 `XDP` 方向的最小参考
- 为后续 `SecurityPolicySkill` 提供 `LSM` 方向的最小参考

推荐优先参考：

- `third_party/reference/lmp-xdp-lsm/xdp/common/`
- `third_party/reference/lmp-xdp-lsm/xdp/xacl_ip/`
- `third_party/reference/lmp-xdp-lsm/lsm/lsm-connect/`
- `third_party/reference/lmp-xdp-lsm/lsm/lsm_bpf_monitoring/`

## 4. kata-lsm-ebpf

来源：

- `D:\code\Ubuntu\Kata-LSM-eBPF`
- 项目快照位置：`third_party/reference/kata-lsm-ebpf`

当前用途：

- 作为 `BPF LSM` 程序组织、策略装载和最小头文件布局的参考
- 为后续 `SecurityPolicySkill` 提供更贴近策略执行的程序样例

推荐优先参考：

- `third_party/reference/kata-lsm-ebpf/varmor_lsm/varmor_lsm.bpf.c`
- `third_party/reference/kata-lsm-ebpf/varmor_lsm/varmor_lsm.c`
- `third_party/reference/kata-lsm-ebpf/varmor_lsm/apply_lsm_policy.c`
- `third_party/reference/kata-lsm-ebpf/kata_lsm_agent/kata_lsm_agent.bpf.c`

## 5. 用户补充可参考仓库

用户明确允许在需要时参考其 GitHub/本地仓库中的以下项目：

- `lmp`：已抽取 `third_party/reference/lmp-xdp-lsm`，本阶段 `network_xdp` 的 XDP hook、map 统计和用户态装载边界参考其中 XDP 样例思路，但生产代码已按 EulerPilot schema v2、libbpf attach/detach 和 isolated veth 测试重新实现。
- `netscope`：后续可用于 Network observability、协议解析、连接路径和统计事件设计参考；当前未直接复制源码到生产路径。
- `lightprobe`：后续可用于轻量运行时观测、动态探针组织和低侵入演示链路参考；当前不进入 Network hot path。
- `katalsm`：后续可用于 Security Agent/Kata 场景下 BPF LSM 事件链路、策略装载和端到端验证参考；当前不直接并入 Network 子能力。

若后续需要把上述仓库的代码并入 EulerPilot，必须先创建最小 reference snapshot，删除构建产物和无关大框架，补充来源说明，再针对 openEuler 24.03-LTS-SP3 编译和测试。

## 6. 当前复用原则

- 参考仓库只解决“怎么设计、怎么拆模块、怎么适配 hook”。
- EulerPilot 生产代码必须按 openEuler 目标环境重新整理目录、构建脚本和接口。
- 当前阶段已完成 `network_policy` connect4、`network_qos` TC egress 和 `network_xdp` isolated-veth XDP 的最小闭环；后续再进入 Security Agent 正式化。
- 下一阶段优先事项是补强：
  - TC QoS 速率误差 Benchmark
  - XDP TCP/UDP 多规则和 Pod veth target
  - Security Agent 的 syscall tracing 与 BPF LSM enforce
  - Resource Control 的 CPU + Memory 自动闭环
