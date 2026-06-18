# lmp-xdp-lsm Reference Snapshot

来源仓库：`D:\code\Ubuntu\lmp`

本目录只保留与 EulerPilot 后续 `network policy` / `security policy` 扩展最相关的最小参考代码，不复制整个 `lmp` 仓库。

## 目录说明

- `xdp/common/`
  - 来自 `eBPF_Supermarket/Network_Subsystem/net_manager/common/`
  - 提供 XDP 用户态装载公共逻辑和统计头文件
- `xdp/xacl_ip/`
  - 来自 `eBPF_Supermarket/Network_Subsystem/net_manager/xacl_ip/`
  - 提供基于 XDP 的 IP ACL 示例
- `lsm/lsm-connect/`
  - 来自 `eBPF_Hub/lsm-connect/`
  - 提供最小 LSM BPF 程序样例
- `lsm/lsm_bpf_monitoring/`
  - 来自 `eBPF_Supermarket/old_project/LSM_BPF/src/`
  - 提供 LSM BPF 程序与对应用户态程序

## 在 EulerPilot 中的建议用途

- `NetworkPolicySkill Demo`
  - 优先参考 `xdp/common/` 和 `xdp/xacl_ip/`
  - 用于做“YAML 驱动 + XDP map/规则下发 + 允许/拒绝”演示
- `SecurityPolicySkill Demo`
  - 优先参考 `lsm/lsm-connect/` 和 `lsm/lsm_bpf_monitoring/`
  - 用于理解 LSM hook 组织方式和最小装载链路

## 复用边界

- 这里只作为接口和结构参考，不直接作为 EulerPilot 生产代码。
- 真正接入前，需要按 openEuler 目标环境重新整理 Makefile、头文件和装载逻辑。
