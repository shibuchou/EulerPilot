# network_policy Skill

职责：Network Policy Agent 的正式 Skill 目录。

## 当前状态

- 已保留 `network_policy_demo` 兼容名称。
- 已新增正式 `network_policy` Skill 注册名。
- `network_policy` 默认 disabled，默认 mode 为 `audit`。
- `audit` 模式不挂载 BPF，不会阻断流量。
- `enforce` 模式当前复用 `cgroup/connect4` demo BPF 程序。

## 当前安全边界

- 只允许作用于 `/sys/fs/cgroup/eulerpilot/demo-net` 这类 demo/lab cgroup。
- XDP 尚未接入正式代码，后续只能挂 isolated veth 或 lab Pod veth。
- 不允许把 XDP 默认挂到 SSH 或管理网卡所在接口。

## 后续任务

1. connect4 路径接入 YAML v2 的 `targets + rules + target_ref`。
2. 将命中事件写入 `AuditBus`。
3. 将 BPF link、pinned map、cgroup 路径写入 `ActionJournal`。
4. 增加 TC QoS。
5. 增加 isolated-veth XDP。
