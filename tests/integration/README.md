# tests/integration

作用：面向单个 Skill 的集成测试入口。

## 当前测试

- `test_network_policy.sh`：验证正式 `network_policy` 注册名、默认 disabled、schema v2 `targets + rules + target_ref`、audit 模式不挂 BPF、doctor 可通过；同时验证 enforce 模式会把 YAML 动态端口写入 BPF map，目标 cgroup 内连接被拒绝，Agent 退出后无 BPF attachment 残留。脚本只使用 Python 标准库，不依赖 PyYAML。
- `test_target_resolver.sh`：验证 `TargetResolver` 的 netdev 解析、`k8s_pod` 错误路径，以及 root 环境下基于临时 netns/veth + fake `kubectl/crictl` 的 container / Pod host veth 解析路径；不依赖真实 Kubernetes。
- `test_network_qos_tc.sh`：验证 `network_qos` 子能力和 schema v2 `netdev target_ref`，创建专用 netns/veth，确认 audit 不改 TC，enforce 会安装 TC clsact + TBF，流量命中 BPF stats，退出后无 qdisc 残留。脚本只使用 Python 标准库，不依赖 PyYAML。
- `test_network_xdp.sh`：验证 `network_xdp` 子能力和 schema v2 `netdev target_ref`，创建专用 netns/veth，确认 audit 不挂 XDP，enforce 以 generic XDP drop ICMP 并命中 TCP:19092 规则，rollback 后 XDP detached 且连通性恢复。脚本只使用 Python 标准库，不依赖 PyYAML。
- `test_security_policy.sh`：验证正式 `security_policy` 注册名、schema v2 path/path_prefix/file_access/exec/exec_prefix/socket/ptrace/capability/setuid/setgid/setgroups/cred_prepare target、最多 8 项 BPF `target_map`、audit 模式 BPF ringbuf hit、`burst_execve` 用户态异常规则、九类 LSM enforce、规则级 `rule_id/target_ref`、显式 cgroup/PID/container_id/runtime container/k8s_pod scope、rollback 和 cleanup。覆盖 scoped IPv4 socket connect、exec_prefix、file_access 写打开、path_prefix 只读目录、ptrace_traceme、CAP_SYS_ADMIN、setuid、setgid、setgroups 与 cred_prepare；对应 blocked 事件分别携带 endpoint、exec_prefix、file_flags、path_prefix、capability、credential 字段和 `cgroup_id/cgroup_path`。当前脚本以 `/root/EulerPilot` 作为 Agent BPF object 与 demo 结果目录基准，在其他路径会安全跳过。121 最新通过结果：`results/security_policy/integration-20260624-114838`；122 最新通过结果：`results/security_policy/integration-20260624-115440`。

## 后续测试

- `test_resource_control.sh`
- `test_sched_ext.sh`
