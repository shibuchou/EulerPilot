# tests

作用：存放 EulerPilot 的集成测试和端到端测试。

## 目录规划

- `integration/`：单个 Skill 或单个能力的集成测试。
- `e2e/`：跨 Agent 联动、完整回滚和演示链路测试。

## 当前状态

- 阶段 B 已开始补 NetworkPolicySkill 测试。
- 测试脚本默认不应影响 SSH、管理网卡、kube-system 或非 lab workload。

## 维护规则

- 新增测试脚本必须说明目标、前置条件、输出位置和清理方式。
- 会 attach BPF、修改 cgroup、TC、XDP 或 LSM 的测试必须提供 rollback。
