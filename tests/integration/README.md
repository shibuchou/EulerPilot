# tests/integration

作用：面向单个 Skill 的集成测试入口。

## 当前测试

- `test_network_policy.sh`：验证正式 `network_policy` 注册名、默认 disabled、schema v2 `targets + rules + target_ref`、audit 模式不挂 BPF、doctor 可通过；同时验证 enforce 模式会把 YAML 动态端口写入 BPF map，目标 cgroup 内连接被拒绝，Agent 退出后无 BPF attachment 残留。脚本只使用 Python 标准库，不依赖 PyYAML。
- `test_network_qos_tc.sh`：验证 `network_qos` 子能力和 schema v2 `netdev target_ref`，创建专用 netns/veth，确认 audit 不改 TC，enforce 会安装 TC clsact + TBF，流量命中 BPF stats，退出后无 qdisc 残留。脚本只使用 Python 标准库，不依赖 PyYAML。
- `test_network_xdp.sh`：验证 `network_xdp` 子能力和 schema v2 `netdev target_ref`，创建专用 netns/veth，确认 audit 不挂 XDP，enforce 以 generic XDP drop ICMP，rollback 后 XDP detached 且连通性恢复。脚本只使用 Python 标准库，不依赖 PyYAML。

## 后续测试

- `test_security_policy.sh`
- `test_resource_control.sh`
- `test_sched_ext.sh`
