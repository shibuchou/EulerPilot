# policy_engine Skill

职责：把不同 Skill 的事件转成受控动作，形成 EulerPilot 的跨 Agent 联动层。

## 当前状态

- 默认 disabled，只在专用配置中启用。
- 已支持 `security_policy -> resource_control` 第一条联动。
- v3.1 已支持 `security_policy -> resource_control + network_qos` 第二条联动。
- 所有跨 Skill 事件写入统一 `transaction_id`、`trigger_event_id` 和 `policy_id`。
- 动作写入 `reports/events/policy_engine.jsonl`、相关 Skill 事件文件和 `run/eulerpilot/action_journal.jsonl`。
- stop/rollback 会恢复 cgroup 旧值，并删除 lab netdev 上的 tc/tbf qdisc。

## v3.1 链路

```text
security_policy burst_connect anomaly
  -> policy_engine decision
  -> resource_control demo_cgroup cpu.max/memory.high
  -> network_qos lab_netdev tc/tbf 2mbit
  -> ActionJournal
  -> rollback
```

## 安全边界

- 不执行外部命令模板，不接受任意 shell 动作。
- cgroup target 必须位于 `/sys/fs/cgroup/`。
- cgroup 文件白名单：`cpu.max`、`cpu.weight`、`memory.high`、`memory.low`、`memory.max`、`io.max`、`io.weight`。
- netdev target 只允许 `ep-*`、`eulerpilot-*`、`lab-*` 前缀，拒绝生产网卡前缀。
- `memory.high` 写入前检查 `memory.max`，避免环境差异导致误失败。
- 多动作事务支持失败回滚。

## 关键文件

- `agent/src/builtin_skills.cpp`：事件监听、动作白名单、事务写入、rollback 实现。
- `configs/policy_engine_security_network_resource.yaml`：v3.1 专用 Agent 配置。
- `configs/policy_engine_security_network_resource.skills.yaml`：v3.1 专用 Skill 配置。
- `tests/integration/test_policy_engine_security_resource.sh`：第一条联动测试。
- `tests/integration/test_policy_engine_security_network_resource.sh`：第二条联动、失败回滚和 repeat 测试。

## 验收

```bash
./build/eulerpilot-agent --validate-config configs/policy_engine_security_network_resource.yaml
sudo tests/integration/test_policy_engine_security_network_resource.sh
sudo tests/integration/test_policy_engine_security_network_resource.sh --repeat 10
```