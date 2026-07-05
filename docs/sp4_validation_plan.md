# openEuler 24.03 LTS SP4 验证计划

更新时间：`2026-07-05`

SP4 已接入为 EulerPilot 后续完整能力验证平台。本文件记录 SP4 初始验证结果、当前限制和下一步 sched_ext/scx 内核验证计划；121/122 的 SP3 双机结果仍作为既有稳定证据保留。

## 当前结果

- SP4 主机：`openEuler-2403-LTS-SP4` / `192.168.1.123`
- 仓库路径：`/root/EulerPilot`
- 验证提交：`d5c3fb3`
- 系统版本：`openEuler 24.03 LTS SP4`
- 内核版本：`6.6.0-159.4.3.154.oe2403sp4.x86_64`
- 已启用启动参数：`systemd.unified_cgroup_hierarchy=1 cgroup_no_v1=all psi=1`
- cgroup v2：已挂载，controllers 包含 `cpu io memory`
- PSI：`cpu/memory/io` 已可用
- BTF / BPF LSM / TC / XDP：能力探测可用
- Web Console：已通过 `npm ci/test/lint/build/audit`，Evidence 显示 28 条、必需缺失 0、警告 0
- 质量门禁：`scripts/final_quality_gate.sh` 已在 SP4 通过 21/21 P0、100 轮 smoke、5 轮 doctor

已保存的 SP4 初始验证证据：

```text
reports/sp4/sp4_initial_validation_20260705-160156.md
reports/sp4/final_quality_gate_20260705-160156.log
reports/sp4/cmdline.before-cgroupv2-20260705-155311.txt
reports/sp4/grub.default.before-cgroupv2-20260705-155311.bak
```

## 当前限制

SP4 当前发行内核未启用 sched_ext：

```text
CONFIG_SCHED_CLASS_EXT is not set
/sys/kernel/sched_ext missing
```

因此当前 SP4 已验证主路径为 `cgroup v2 + PSI + eBPF + Policy Engine + Web Console`。下一步将基于 SP4 内核源码重新编译启用 `CONFIG_SCHED_CLASS_EXT` 的内核，并在新内核上复核 scx/sched_ext 路径。

## 目标

在 SP4 测试机上验证 EulerPilot 的基础能力兼容性，并补齐 sched_ext/scx 增强路径：

- Agent 编译与基础启动。
- cgroup v2 CPU/Memory/IO controller。
- BTF、BPF LSM、XDP、TC、sched_ext/scx 能力探测。
- Security、Network、Resource Control、Policy Engine 的核心集成测试。
- 自编译启用 `CONFIG_SCHED_CLASS_EXT` 的 SP4 内核，验证 `/sys/kernel/sched_ext` 与 scx 相关能力。

## 不阻塞事项

- sched_ext/scx 不阻塞既有 SP3 证据；但 SP4 将作为后续完整能力验证平台继续推进。
- Kubernetes/真实 runtime/真实 Pod veth 仍作为 v3.2 第一优先级，不放入 v3.1 完成条件。

## 检查入口

```bash
scripts/check_sp4_env.sh
```

脚本只做环境探测，不修改系统状态。若检测到 SP4，再继续执行：

```bash
make agent
./build/eulerpilot-agent --validate-config configs/agent.yaml
./build/eulerpilot-agent --list-skills
./build/eulerpilot-agent --doctor-skills --config configs/agent.yaml
sudo tests/integration/test_policy_engine_security_network_resource.sh
sudo scripts/final_quality_gate.sh
```

## 结果记录

SP4 验证结果建议保存到：

```text
results/sp4-validation/<timestamp>/
```

至少包含：

- `os-release.txt`
- `uname.txt`
- `check_sp4_env.log`
- `agent_validate_config.log`
- `doctor_skills.log`
- `policy_engine_security_network_resource.log`
- `final_quality_gate.log`
- `summary.md`

## 发布链接记录

SP4 正式发布后再补入：

- openEuler 下载页：待确认
- SP4 Release Notes：待确认
- SP4 ISO/镜像源：待确认
- 对应 kernel、bpftool、clang/llvm 版本：待确认
