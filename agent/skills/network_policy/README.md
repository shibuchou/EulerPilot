# network_policy Skill

职责：Network Policy Agent 的正式 Skill 目录。

## 当前状态

- 已保留 `network_policy_demo` 兼容名称。
- 已新增正式 `network_policy` Skill 注册名。
- `network_policy` 默认 disabled，默认 mode 为 `audit`。
- 默认 `configs/skills.yaml` 已使用 `schema_version: 2` 的 `targets + rules + target_ref`。
- `audit` 模式不挂载 BPF，不会阻断流量。
- `enforce` 模式复用 `cgroup/connect4` BPF 子能力，端口由 YAML 写入 BPF map，不再依赖硬编码常量。
- 已接入基础命中统计：`stats_map` 记录 allow/deny 计数，rollback 事件会带上最终计数。
- 已接入 `TargetResolver`、`AuditBus` 和 `ActionJournal` 的最小闭环。
- `tests/integration/test_network_policy.sh` 已覆盖 YAML v2、audit、enforce、动态端口拒绝和 rollback 无残留。
- 已新增 `network_qos` 子能力：在专用 lab veth 上挂 `tc_egress` BPF classifier，并通过 TBF qdisc 执行最小限速闭环。
- `tests/integration/test_network_qos_tc.sh` 已覆盖 YAML v2、lab netns/veth、TC clsact + TBF、BPF 命中统计和 rollback 无残留。
- 已新增 `network_xdp` 子能力：在专用 isolated veth 上挂 generic XDP 程序，支持 ICMP drop/pass 和 BPF 命中统计。
- `tests/integration/test_network_xdp.sh` 已覆盖 YAML v2、lab netns/veth、audit 不挂 XDP、enforce 丢 ICMP、rollback 后连通性恢复。

## 当前安全边界

- 只允许作用于 `/sys/fs/cgroup/eulerpilot/demo-net` 这类 demo/lab cgroup。
- TC QoS 默认只允许作用于 `ep-veth-qos0` 这类测试 veth；不默认修改真实业务网卡或管理网卡。
- XDP 默认只允许作用于 `ep-veth-xdp0` 这类 isolated veth；后续扩展到 lab Pod veth 前必须先经过 target resolver 校验。
- 不允许把 XDP 默认挂到 SSH 或管理网卡所在接口。

## 后续任务

1. 将命中级事件从“启动/回滚统计”升级为按规则聚合输出。
2. 将 TC QoS 从单 lab veth 扩展到多规则和速率误差 Benchmark。
3. 将 isolated-veth XDP 从 ICMP demo 扩展到 TCP/UDP 多规则和 Pod veth。
4. 将 `TargetResolver` 扩展到 netdev/k8s_pod。
