# NetworkQos TC YAML v2 集成测试记录

生成时间：`2026-06-19 12:09:16`

执行环境：

- 主机：`192.168.1.121`
- 系统：openEuler 24.03 LTS SP3
- 内核：`6.6.0-132.0.0.111.oe2403sp3.x86_64`

执行命令：

```bash
bash tests/integration/test_network_qos_tc.sh
```

覆盖内容：

- `configs/skills.yaml` 使用 `schema_version: 2`。
- `network_qos` 通过 `targets.lab_veth` 和 `rules[].target_ref` 定位 lab veth。
- 测试创建专用 netns/veth：host 侧 `ep-veth-qos0`，peer 侧 `ep-veth-qos1`。
- audit 模式不修改 TC qdisc。
- enforce 模式安装 TC clsact + TBF。
- ping 流量命中 TC BPF stats，rollback 事件记录 `packet_count=3`、`byte_count=294`。
- 审计事件包含 `rule_id=limit_lab_egress` 和 `target_ref=lab_veth`。
- Agent 退出后无 TC qdisc 残留。

结论边界：

- 本目录证明 TC QoS 最小闭环已接入 YAML v2 目标/规则模型。
- 多规则、Pod/veth 自动解析和速率误差 Benchmark 仍属于后续工作。
