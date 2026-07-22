# kata-lsm-ebpf Reference Snapshot

来源仓库：`<reference-source>/Kata-LSM-eBPF`

本目录只摘出与 `BPF LSM` 和策略装载最相关的 eBPF 程序与最小用户态参考，不复制 Kubernetes、控制面和历史备份垃圾。

## 目录说明

- `kata_lsm_agent/`
  - 来自 `old_previous_files_20260523/bpf-daemon/`
  - 保留 `kata_lsm_agent.bpf.c` 与 `varmor_lsm_bpf_embed.h`
- `varmor_lsm/`
  - 来自 `old_previous_files_20260523/vArmor_lsm/`
  - 保留 `varmor_lsm.bpf.c`、用户态装载程序、头文件和最小构建脚本

## 在 EulerPilot 中的建议用途

- `SecurityPolicySkill` 的 LSM hook 参考
  - 重点参考 `varmor_lsm.bpf.c`
  - 观察其 hook 组织、事件上报和策略检查方式
- 用户态装载与策略下发参考
  - 重点参考 `varmor_lsm.c`、`apply_lsm_policy.c`
- 多程序组织参考
  - 可对比 `kata_lsm_agent.bpf.c` 与 `varmor_lsm.bpf.c` 的职责划分

## 复用边界

- 本目录是“reference snapshot”，不是现成可直接并入 EulerPilot 的生产模块。
- 原始代码面向 Android / Kata / vArmor 相关上下文，接入 EulerPilot 前必须针对 openEuler 的工具链、头文件、hook 能力和运行模式重新适配。
