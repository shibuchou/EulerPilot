# tests/integration

作用：面向单个 Skill 的集成测试入口。

## 当前测试

- `test_network_policy.sh`：验证正式 `network_policy` 注册名、默认 disabled、schema v2 `targets + rules + target_ref`、audit 模式不挂 BPF、doctor 可通过；同时验证 enforce 模式会把 YAML 动态端口写入 BPF map，目标 cgroup 内连接被拒绝，Agent 退出后无 BPF attachment 残留。脚本只使用 Python 标准库，不依赖 PyYAML。
- `test_target_resolver.sh`：验证 `TargetResolver` 的 netdev 解析和 `k8s_pod` 错误路径，不依赖 Kubernetes、不需要 root、不修改系统网络。
- `test_network_qos_tc.sh`：验证 `network_qos` 子能力和 schema v2 `netdev target_ref`，创建专用 netns/veth，确认 audit 不改 TC，enforce 会安装 TC clsact + TBF，流量命中 BPF stats，退出后无 qdisc 残留。脚本只使用 Python 标准库，不依赖 PyYAML。
- `test_network_xdp.sh`：验证 `network_xdp` 子能力和 schema v2 `netdev target_ref`，创建专用 netns/veth，确认 audit 不挂 XDP，enforce 以 generic XDP drop ICMP 并命中 TCP:19092 规则，rollback 后 XDP detached 且连通性恢复。脚本只使用 Python 标准库，不依赖 PyYAML。
- `test_security_policy.sh`：验证正式 `security_policy` 注册名、schema v2 path/exec/exec_prefix/socket target、用户态从 YAML target path、exec path、exec_prefix、dst_ip、dst_port、可选 cgroup_path、`type: pid`、`type: container_id`、`type: container` 和 `type: k8s_pod` 解析结果填充最多 8 项 BPF `target_map`、audit 模式 attach BPF 但不阻断，并写入 `lsm_file_open`、`lsm_bprm_check_security`、`sys_enter_execve`、`sys_enter_openat`、`sys_enter_connect`、`sys_enter_ptrace` 六类 ringbuf hit 事件；enforce 模式 BPF LSM attach 后目标文件、demo 可执行文件、目标 cgroup 内 exec_prefix 匹配的可写目录脚本和 scoped IPv4 socket connect 拒绝并写 blocked hit 事件；随后创建 `/tmp/eulerpilot-security-policy.*` 下两组动态目标，验证 YAML `path/exec_path` 真实驱动多目标 `target_map`，且 LSM blocked 事件分别带上对应的 `rule_id/target_ref`，原 demo 目标不会被误阻断；再创建临时 cgroup 验证显式 cgroup、PID、container_id、runtime container name 和 k8s pod name 都能解析到 cgroup scope，且 scope 外允许、scope 内阻断、事件带 `cgroup_id/cgroup_path`；socket 用例启动本地 TCP server，验证 `lsm_socket_connect` blocked 事件带 `dst_ip/dst_port/protocol`；exec_prefix 用例验证 blocked 事件带 `exec_prefix`；Agent 退出恢复和 cleanup 无残留；`security_policy_demo` 作为兼容名保留。当前脚本以 `/root/EulerPilot` 作为 Agent BPF object 与 demo 结果目录基准，在其他路径会安全跳过。121 最新通过结果：`results/security_policy/integration-20260622-145403`；122 最新通过结果：`results/security_policy/integration-20260622-145716`。

## 后续测试

- `test_resource_control.sh`
- `test_sched_ext.sh`
