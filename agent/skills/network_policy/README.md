# network_policy Skill

职责：Network Policy Agent 的正式 Skill 目录。

## 当前状态

- 已保留 `network_policy_demo` 兼容名称。
- 已新增正式 `network_policy` Skill 注册名。
- `network_policy` 默认 disabled，默认 mode 为 `audit`。
- `audit` 模式不挂载 BPF，不会阻断流量。
- `enforce` 模式复用 `cgroup/connect4` BPF 子能力，端口由 YAML 写入 BPF map，不再依赖硬编码常量。
- 已接入基础命中统计：`stats_map` 记录 allow/deny 计数，rollback 事件会带上最终计数。
- 已接入 `TargetResolver`、`AuditBus` 和 `ActionJournal` 的最小闭环。
- `tests/integration/test_network_policy.sh` 已覆盖 audit、enforce、动态端口拒绝和 rollback 无残留。
- 已新增 `network_qos` 子能力：在专用 lab veth 上挂 `tc_egress` BPF classifier，并通过 TBF qdisc 执行最小限速闭环。
- `tests/integration/test_network_qos_tc.sh` 已覆盖 lab netns/veth、TC clsact + TBF、BPF 命中统计和 rollback 无残留。

## 当前安全边界

- 只允许作用于 `/sys/fs/cgroup/eulerpilot/demo-net` 这类 demo/lab cgroup。
- TC QoS 默认只允许作用于 `ep-veth-qos0` 这类测试 veth；不默认修改真实业务网卡或管理网卡。
- XDP 尚未接入正式代码，后续只能挂 isolated veth 或 lab Pod veth。
- 不允许把 XDP 默认挂到 SSH 或管理网卡所在接口。

## 后续任务

1. connect4 路径接入 YAML v2 的 `targets + rules + target_ref`。
2. 将命中级事件从“启动/回滚统计”升级为按规则聚合输出。
3. 将 TC QoS 从单 lab veth 扩展到 YAML v2 的 `target_ref` 与多规则。
4. 增加 isolated-veth XDP。
