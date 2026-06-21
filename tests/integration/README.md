# tests/integration

作用：面向单个 Skill 的集成测试入口。

## 当前测试

- `test_network_policy.sh`：验证正式 `network_policy` 注册名、默认 disabled、schema v2 `targets + rules + target_ref`、audit 模式不挂 BPF、doctor 可通过；同时验证 enforce 模式会把 YAML 动态端口写入 BPF map，目标 cgroup 内连接被拒绝，Agent 退出后无 BPF attachment 残留。脚本只使用 Python 标准库，不依赖 PyYAML。
- `test_target_resolver.sh`：验证 `TargetResolver` 的 netdev 解析和 `k8s_pod` 错误路径，不依赖 Kubernetes、不需要 root、不修改系统网络。
- `test_network_qos_tc.sh`：验证 `network_qos` 子能力和 schema v2 `netdev target_ref`，创建专用 netns/veth，确认 audit 不改 TC，enforce 会安装 TC clsact + TBF，流量命中 BPF stats，退出后无 qdisc 残留。脚本只使用 Python 标准库，不依赖 PyYAML。
- `test_network_xdp.sh`：验证 `network_xdp` 子能力和 schema v2 `netdev target_ref`，创建专用 netns/veth，确认 audit 不挂 XDP，enforce 以 generic XDP drop ICMP 并命中 TCP:19092 规则，rollback 后 XDP detached 且连通性恢复。脚本只使用 Python 标准库，不依赖 PyYAML。
- `test_security_policy.sh`：验证正式 `security_policy` 注册名、schema v2 path target、audit 模式 attach BPF 但不阻断，并写入 `lsm_file_open`、`sys_enter_execve`、`sys_enter_openat`、`sys_enter_connect`、`sys_enter_ptrace` 五类 ringbuf hit 事件；enforce 模式 BPF LSM attach 后目标文件拒绝并写 blocked hit 事件；Agent 退出恢复和 cleanup 无残留；`security_policy_demo` 作为兼容名保留。当前 BPF demo 硬编码 `/root/EulerPilot`，在其他路径会安全跳过。

## 后续测试

- `test_resource_control.sh`
- `test_sched_ext.sh`
