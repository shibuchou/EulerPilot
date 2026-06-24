# tests/integration

作用：面向单个 Skill 的集成测试入口。

## 当前测试

- `test_network_policy.sh`：验证正式 `network_policy` 注册名、默认 disabled、schema v2 `targets + rules + target_ref`、audit 模式不挂 BPF、doctor 可通过；同时验证 enforce 模式会把 YAML 动态端口写入 BPF map，目标 cgroup 内连接被拒绝，Agent 退出后无 BPF attachment 残留。脚本只使用 Python 标准库，不依赖 PyYAML。
- `test_target_resolver.sh`：验证 `TargetResolver` 的 netdev 解析、`k8s_pod` 错误路径，以及 root 环境下基于临时 netns/veth + fake `kubectl/crictl` 的 container / Pod host veth 解析路径；不依赖真实 Kubernetes。
- `test_network_qos_tc.sh`：验证 `network_qos` 子能力和 schema v2 `netdev target_ref`，创建专用 netns/veth，确认 audit 不改 TC，enforce 会安装 TC clsact + TBF，流量命中 BPF stats，退出后无 qdisc 残留。脚本只使用 Python 标准库，不依赖 PyYAML。
- `test_network_xdp.sh`：验证 `network_xdp` 子能力和 schema v2 `netdev target_ref`，创建专用 netns/veth，确认 audit 不挂 XDP，enforce 以 generic XDP drop ICMP 并命中 TCP:19092 规则，rollback 后 XDP detached 且连通性恢复。脚本只使用 Python 标准库，不依赖 PyYAML。
- `test_security_policy.sh`：验证正式 `security_policy` 注册名、schema v2 path/path_prefix/file_access/exec/exec_prefix/socket/ptrace/capability/setuid/setgid/setgroups/cred_prepare target、最多 8 项 BPF `target_map`、audit 模式 BPF ringbuf hit、`burst_execve` 用户态异常规则、九类 LSM enforce、规则级 `rule_id/target_ref`、显式 cgroup/PID/container_id/runtime container/k8s_pod scope、rollback 和 cleanup。覆盖 scoped IPv4 socket connect、exec_prefix、file_access 写打开、path_prefix 只读目录、ptrace_traceme、CAP_SYS_ADMIN、setuid、setgid、setgroups 与 cred_prepare；对应 blocked 事件分别携带 endpoint、exec_prefix、file_flags、path_prefix、capability、credential 字段和 `cgroup_id/cgroup_path`。当前脚本以 `/root/EulerPilot` 作为 Agent BPF object 与 demo 结果目录基准，在其他路径会安全跳过。121 最新通过结果：`results/security_policy/integration-20260624-114838`；122 最新通过结果：`results/security_policy/integration-20260624-115440`。
- `test_resource_control.sh`：验证正式 `resource_control` CPU+Memory 自动闭环。脚本初始化 cgroup v2 CPU/cpuset/memory controller，启动 background workload，以 `always-active + --active` 运行 Agent，确认 background 组写入 `cpu.max=10000 100000` 与 `memory.high=1048576`，用内存压力触发 `memory.events high` 计数增长，并确认 Agent 停止后恢复旧值。121 最新通过结果：`results/resource_control/integration-20260624-160317`；122 最新通过结果：`results/resource_control/integration-20260624-160349`。
- `test_resource_control_io.sh`：验证正式 `resource_control` IO controller。脚本初始化 cgroup v2 IO controller，检测根文件系统块设备，确认 background 组写入 `io.max=253:0 rbps=max wbps=1048576` 与 `io.weight=default 50`，用 direct write 对比 baseline/limited 耗时和 `io.stat wbytes`，并确认 Agent 停止后恢复旧值。121 最新通过结果：`results/resource_control/io-20260624-160008`；122 最新通过结果：`results/resource_control/io-20260624-160208`。
- `test_resource_control_target.sh`：验证正式 `resource_control` 的 `target_ref` cgroup 闭环。脚本创建目标 cgroup 和非目标 cgroup，将两个 background workload 分别放入其中，确认只对 `profiles.background.target_ref` 指向的 cgroup 写 `cpu.max/memory.high`，非目标 cgroup 不被误改，审计和 Agent JSONL 均携带 `target_ref`。121 最新通过结果：`results/resource_control/target-20260624-172139`；122 最新通过结果：`results/resource_control/target-20260624-172916`。
- `test_resource_control_runtime_target.sh`：验证正式 `resource_control` 的 runtime target 解析闭环。脚本使用 fake `crictl/kubectl` 固定解析路径，分别验证 `type: container_id`、`type: container` 和 `type: k8s_pod` 能解析到目标 cgroup，只对目标 cgroup 写 `cpu.max/memory.high`，非目标 cgroup 不被误改，并确认 Agent 退出后恢复旧值。121 最新通过结果：`results/resource_control/runtime-target-20260624-212403`；122 最新通过结果：`results/resource_control/runtime-target-20260624-212529`。

## 后续测试

- `test_sched_ext.sh`
