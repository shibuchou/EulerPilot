# tests

作用：存放 EulerPilot 的集成测试和端到端测试。

## 目录规划

- `integration/`：单个 Skill 或单个能力的集成测试。
- `benchmark/`：会输出可量化指标的阶段性 Benchmark，不默认进入 P0 质量门禁。
- `e2e/`：跨 Agent 联动、完整回滚和演示链路测试。

## 当前状态

- 阶段 B 已开始补 NetworkPolicySkill 测试。
- `tests/benchmark/test_network_qos_rate.sh` 已用于验证 TC QoS 的速率误差和限速倍数。
- 测试脚本默认不应影响 SSH、管理网卡、kube-system 或非 lab workload。

## 维护规则

- 新增测试脚本必须说明目标、前置条件、输出位置和清理方式。
- 会 attach BPF、修改 cgroup、TC、XDP 或 LSM 的测试必须提供 rollback。
