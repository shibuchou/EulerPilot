# EulerPilot 参考仓库说明

更新时间：`2026-06-12`

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

## 5. 当前复用原则

- 参考仓库只解决“怎么设计、怎么拆模块、怎么适配 hook”。
- EulerPilot 生产代码必须按 openEuler 目标环境重新整理目录、构建脚本和接口。
- 当前阶段不同时开工 `Network Policy Agent` 与 `Security Policy Agent`。
- 下一阶段优先事项是补强：
  - 统一 `Skill` 接口
  - `SkillRegistry`
  - `skills.yaml` / 能力 YAML 的真实驱动能力
  - 一个隔离的 eBPF 扩展示例
