# NetworkQos TC 集成测试记录

生成时间：`2026-06-18 21:37:09`

执行环境：

- 主机：`192.168.1.121`
- 系统：openEuler 24.03 LTS SP3
- 内核：`6.6.0-132.0.0.111.oe2403sp3.x86_64`

执行命令：

```bash
bash tests/integration/test_network_qos_tc.sh
```

覆盖内容：

- `network_qos` 正式子能力可被 `--list-skills` 枚举。
- 测试创建专用 netns/veth：host 侧 `ep-veth-qos0`，peer 侧 `ep-veth-qos1`。
- audit 模式不修改 TC qdisc。
- enforce 模式安装 TC clsact + TBF。
- ping 流量命中 TC BPF stats，rollback 事件记录 `packet_count=3`、`byte_count=294`。
- Agent 退出后无 TC qdisc 残留。

结论边界：

- 本目录只证明 TC QoS 最小闭环：BPF classifier 统计命中，TBF 执行限速挂载和回滚。
- 尚未证明多规则、Pod/veth 自动解析、速率误差 Benchmark，也不代表可以挂真实管理网卡。
