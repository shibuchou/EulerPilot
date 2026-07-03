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
- 已新增 `network_xdp` 子能力：在专用 isolated veth 上挂 generic XDP 程序，支持最多 8 条规则、协议/源 IP/目的 IP/源端口/目的端口匹配和 BPF 命中统计。
- `tests/integration/test_network_xdp.sh` 已覆盖 YAML v2、lab netns/veth、audit 不挂 XDP、enforce 丢 ICMP、enforce 丢 TCP:19092、enforce 丢 UDP:19093、enforce 丢 UDP tuple `10.89.0.2:39094 -> 10.89.0.1:19094`、rollback per-rule 统计和连通性恢复。
- `TargetResolver` 已支持 `type: container` 和 `type: k8s_pod` 到 host veth ifname/ifindex 的解析路径，`network_qos` 与 `network_xdp` 的 v2 target 可接受容器和 Pod target。
- `tests/integration/test_network_xdp_real_pod_veth.sh` 已在 121/122 真实 k3s lab Pod 上通过，验证 Pod host veth generic XDP attach、ICMP/TCP/UDP 与 UDP tuple 四规则 drop、per-rule 字段统计和 rollback。

## 当前安全边界

- 只允许作用于 `/sys/fs/cgroup/eulerpilot/demo-net` 这类 demo/lab cgroup。
- TC QoS 默认只允许作用于 `ep-veth-qos0` 这类测试 veth；不默认修改真实业务网卡或管理网卡。
- XDP 默认只允许作用于 `ep-veth-xdp0` 这类 isolated veth；扩展到显式容器 veth 或 lab Pod veth 时必须先经过 target resolver 校验，并保持 allowlist/namespace 约束。
- 不允许把 XDP 默认挂到 SSH 或管理网卡所在接口。

## 后续任务

1. 将命中级事件从“启动/回滚统计”升级为按规则聚合输出。
2. 将 TC QoS 从单 lab veth 扩展到多规则和速率误差 Benchmark。
3. isolated-veth 与 real Pod host veth XDP 均已完成协议、源/目的 IP、源/目的端口 tuple 多字段匹配；下一步转入答辩证据压缩。
4. 将 Network 证据整理为答辩现场短链路演示材料。

## v3.1 network_qos 联动

`network_qos` 已作为 Policy Engine 第二条联动动作之一。Policy Engine 只允许对 `ep-*`、`eulerpilot-*`、`lab-*` 前缀的 lab netdev 写入 tc/tbf qdisc；v3.1 测试脚本自行创建 `ep-veth-pe0 <-> ep-veth-pe1`，并在 Agent 停止后删除 qdisc。
