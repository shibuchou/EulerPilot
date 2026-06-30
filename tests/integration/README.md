# tests/integration

作用：面向单个 Skill 的集成测试入口。

## 当前测试

- `test_network_policy.sh`：验证正式 `network_policy` 注册名、默认 disabled、schema v2 `targets + rules + target_ref`、audit 模式不挂 BPF、doctor 可通过；同时验证 enforce 模式会把 YAML 动态端口写入 BPF map，目标 cgroup 内连接被拒绝，Agent 退出后无 BPF attachment 残留。脚本只使用 Python 标准库，不依赖 PyYAML。
- `test_target_resolver.sh`：验证 `TargetResolver` 的 netdev 解析、`k8s_pod` 错误路径，以及 root 环境下基于临时 netns/veth + fake `kubectl/crictl` 的 container / Pod host veth 解析路径；不依赖真实 Kubernetes。
- `test_network_qos_tc.sh`：验证 `network_qos` 子能力和 schema v2 `netdev target_ref`，创建专用 netns/veth，确认 audit 不改 TC，enforce 会安装 TC clsact + TBF，流量命中 BPF stats，退出后无 qdisc 残留。脚本只使用 Python 标准库，不依赖 PyYAML。
- `test_network_qos_real_pod_veth.sh`：真实 Kubernetes lab Pod host veth QoS 演示入口。`kubectl`、CRI 和 `eulerpilot-lab` demo Pod 可用时，通过 `type: k8s_pod + namespace + pod_name` 解析 Pod host veth，在该 veth 上安装 TC/TBF，发送本机到 Pod 流量并验证 rollback 无 qdisc 残留。121 当前结果：`results/network_policy/real-pod-veth-qos-20260630-k3s-121-v2`；122 当前结果：`results/network_policy/real-pod-veth-qos-20260630-k3s-122-v1`。
- `test_network_xdp.sh`：验证 `network_xdp` 子能力和 schema v2 `netdev target_ref`，创建专用 netns/veth，确认 audit 不挂 XDP，enforce 以 generic XDP drop ICMP 并命中 TCP:19092 规则，rollback 后 XDP detached 且连通性恢复。脚本只使用 Python 标准库，不依赖 PyYAML。
- `test_security_policy.sh`：验证正式 `security_policy` 注册名、schema v2 path/path_prefix/file_access/exec/exec_prefix/socket/ptrace/capability/setuid/setgid/setgroups/cred_prepare target、最多 8 项 BPF `target_map`、audit 模式 BPF ringbuf hit、`burst_execve` 用户态异常规则、九类 LSM enforce、规则级 `rule_id/target_ref`、显式 cgroup/PID/container_id/runtime container/k8s_pod scope、rollback 和 cleanup。覆盖 scoped IPv4 socket connect、exec_prefix、file_access 写打开、path_prefix 只读目录、ptrace_traceme、CAP_SYS_ADMIN、setuid、setgid、setgroups 与 cred_prepare；对应 blocked 事件分别携带 endpoint、exec_prefix、file_flags、path_prefix、capability、credential 字段和 `cgroup_id/cgroup_path`。当前脚本以 `/root/EulerPilot` 作为 Agent BPF object 与 demo 结果目录基准，在其他路径会安全跳过。121 最新通过结果：`results/security_policy/integration-20260624-114838`；122 最新通过结果：`results/security_policy/integration-20260624-115440`。
- `test_resource_control.sh`：验证正式 `resource_control` CPU+Memory 自动闭环。脚本初始化 cgroup v2 CPU/cpuset/memory controller，启动 background workload，以 `always-active + --active` 运行 Agent，确认 background 组写入 `cpu.max=10000 100000` 与 `memory.high=1048576`，用内存压力触发 `memory.events high` 计数增长，并确认 Agent 停止后恢复旧值。121 最新通过结果：`results/resource_control/integration-20260624-160317`；122 最新通过结果：`results/resource_control/integration-20260624-160349`。
- `test_resource_control_io.sh`：验证正式 `resource_control` IO controller。脚本初始化 cgroup v2 IO controller，检测根文件系统块设备，确认 background 组写入 `io.max=253:0 rbps=max wbps=1048576` 与 `io.weight=default 50`，用 direct write 对比 baseline/limited 耗时和 `io.stat wbytes`，并确认 Agent 停止后恢复旧值。121 最新通过结果：`results/resource_control/io-20260624-160008`；122 最新通过结果：`results/resource_control/io-20260624-160208`。
- `test_resource_control_target.sh`：验证正式 `resource_control` 的 `target_ref` cgroup 闭环。脚本创建目标 cgroup 和非目标 cgroup，将两个 background workload 分别放入其中，确认只对 `profiles.background.target_ref` 指向的 cgroup 写 `cpu.max/memory.high`，非目标 cgroup 不被误改，审计和 Agent JSONL 均携带 `target_ref`。121 最新通过结果：`results/resource_control/target-20260624-172139`；122 最新通过结果：`results/resource_control/target-20260624-172916`。
- `test_resource_control_runtime_target.sh`：验证正式 `resource_control` 的 runtime target 解析闭环。脚本使用 fake `crictl/kubectl` 固定解析路径，分别验证 `type: container_id`、`type: container` 和 `type: k8s_pod` 能解析到目标 cgroup，只对目标 cgroup 写 `cpu.max/memory.high`，非目标 cgroup 不被误改，并确认 Agent 退出后恢复旧值。121 最新通过结果：`results/resource_control/runtime-target-20260630-113310`；122 最新通过结果：`results/resource_control/runtime-target-20260630-113354`。
- `test_resource_control_runtime_readiness.sh`：只读诊断真实 runtime / Kubernetes lab 是否具备现场实测条件。脚本检查 docker/podman/isula/nerdctl/ctr/crictl/kubectl 命令、systemd 服务、CRI/Docker/iSulad socket、runtime cgroup 和基础 `ps/get namespace` 探测；没有 runtime 时输出 `result=blocked` 而不失败。121 当前诊断结果：`results/resource_control/runtime-readiness-20260630-k3s-121`；122 当前诊断结果：`results/resource_control/runtime-readiness-20260630-k3s-122`，两台机器均已具备 Podman runtime 与 k3s Kubernetes lab。
- `test_resource_control_real_runtime_target.sh`：真实 docker/podman/iSulad 容器 target 演示入口。runtime 和本地镜像可用时启动 `busybox` CPU workload，通过 `type: container + container_name + runtime` 验证 `target_ref`、`cpu.max/memory.high` 写入、审计和 rollback；缺 runtime 或镜像时输出 `result=blocked`，不会自动安装软件或拉镜像。121 当前结果：`results/resource_control/real-runtime-target-20260630-podman-121-final2`；122 当前结果：`results/resource_control/real-runtime-target-20260630-podman-122-final2`，两台均为 `result=pass`。
- `test_resource_control_real_pod_target.sh`：真实 Kubernetes lab Pod target 演示入口。`kubectl` 和 `eulerpilot-lab` demo Pod 可用时通过 `type: k8s_pod + namespace + pod_name` 验证 Pod cgroup 写入、审计和 rollback；默认只使用已有 Pod，只有显式设置 `EULERPILOT_ALLOW_K8S_CREATE=1` 才创建 demo Pod。121 当前结果：`results/resource_control/real-pod-target-20260630-k3s-121-v2`；122 当前结果：`results/resource_control/real-pod-target-20260630-k3s-122-v1`，两台均为 `result=pass`。
- `test_resource_control_cpu_quota.sh`：验证正式 `resource_control` 的 CPU quota 效果指标。脚本先在 `cpu.max=max` 下采样 CPU hog 的 `cpu.stat usage_usec`，再让 Agent 写入 `cpu.max=10000 100000`，验证限额后单位时间 CPU 使用量下降、`nr_throttled/throttled_usec` 增长，并确认 rollback 恢复旧值。121 最新通过结果：`results/resource_control/cpu-quota-20260625-095030`；122 最新通过结果：`results/resource_control/cpu-quota-20260625-095114`。
- `test_policy_engine_security_resource.sh`：验证正式 `policy_engine` 的 Security anomaly -> Resource Control 降级联动。脚本启用 `security_policy` 的 `burst_execve` anomaly 和 `policy_engine` enforce，触发异常后确认目标 cgroup 写入 `cpu.max=10000 100000` 与 `memory.high=1048576`，`policy_engine_events.jsonl` 包含 `cross_skill_response result=applied/restored`，`ActionJournal` 记录旧值和新值，Agent 停止后恢复 `cpu.max=max 100000` 与 `memory.high=max`。121 最新通过结果：`results/policy_engine/security-resource-20260629-163949`；122 最新通过结果：`results/policy_engine/security-resource-20260629-164135`。

## 后续测试

- `test_sched_ext.sh`

## Policy Engine Security + Network + Resource

新增 v3.1 集成测试：

```bash
sudo tests/integration/test_policy_engine_security_network_resource.sh
sudo tests/integration/test_policy_engine_security_network_resource.sh --repeat 10
```

覆盖链路：`security_policy burst_connect -> policy_engine -> resource_control cpu.max/memory.high -> network_qos tc/tbf -> rollback`。脚本会创建 isolated veth 和 lab cgroup，验证 `transaction_id` 可串起 security、policy_engine、resource_control、network_qos 和 ActionJournal，并包含 Resource 成功但 Network 失败时的回滚测试。
