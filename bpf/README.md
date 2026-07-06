# eBPF 观测模块

本目录用于实现 EulerPilot 的 workload 行为观测程序。

## 当前状态

- 第一版 CO-RE observer 已可构建并运行。
- 已验证 `tp_btf/sched_wakeup`、`tp_btf/sched_switch`、`tp_btf/sched_migrate_task` 可在当前 openEuler 24.03 LTS SP3 内核上加载。
- 当前通过 `task_metrics_map` 按 PID 聚合观测数据，并由 `build/workload_observer_dump` 或主 Agent 读取输出。
- `network_policy.bpf.c` 提供 `cgroup/connect4` 子能力，当前用于正式 `network_policy` 的端口拒绝与统计。
- `network_qos_tc.bpf.c` 提供 `tc_egress` classifier 子能力，当前用于 `network_qos` 的 lab veth QoS 命中统计，限速由 TBF qdisc 执行。
- `network_xdp.bpf.c` 提供 `xdp` 子能力，当前用于 `network_xdp` 在 isolated veth 上执行最多 8 条规则的 ICMP/TCP drop/pass 和命中统计；默认不挂真实业务网卡。

## 当前已采集的证据

- `wakeup_count`
- `total_wait_ns`
- `runtime_ns`
- `ctx_switch_count`
- `migrate_count`
- `last_cpu`

这些证据主要用于：

- 识别延迟敏感 workload
- 识别后台干扰 workload
- 生成调度等待和运行时行为证据

## 第一版目标

- 调度事件采集：
  - `sched_wakeup`
  - `sched_switch`
  - `sched_migrate_task`
- task 级等待/运行/唤醒/迁核统计
- map dump 输出给用户态 Agent

## 快速验证

```bash
make observer
timeout 5s ./build/workload_observer_dump
```

工程底座优先参考 `libbpf-bootstrap`。

Network 子能力快速构建：

```bash
make network-policy network-qos-tc network-xdp
```

Security policy 快速构建：

```bash
make security-policy
```

`security_policy.bpf.c` 是当前 SecurityPolicySkill 的最小 BPF 程序，包含 `lsm/file_open` 文件访问强制控制、`lsm/bprm_check_security` 程序执行强制控制、`lsm/socket_connect` IPv4 endpoint 强制控制、`lsm/ptrace_traceme` scoped ptrace 强制控制、`lsm/capable` scoped capability 强制控制、`lsm/task_fix_setuid` scoped setuid credential 强制控制、`lsm/task_fix_setgid` scoped setgid credential 强制控制、`lsm/task_fix_setgroups` scoped supplementary groups 强制控制和 `sys_enter_execve/sys_enter_openat/sys_enter_connect/sys_enter_ptrace` tracepoint 观测。它已经提供 `policy_map.enforce/target_count`、最多 8 项 `BPF_ARRAY target_map` 和 ringbuf 命中事件：audit 模式允许访问并记录 `observed`，同时输出 `lsm_file_open`、`lsm_bprm_check_security` 与四类 syscall 事件；enforce 模式在用户态写入的 path/path_prefix/file_access/exec/exec_prefix/socket/ptrace/capability/setuid/setgid/setgroups/cred_prepare 目标上拒绝访问并记录 `blocked`。`target_map` 每项可选 `cgroup_id`，未配置时按路径、路径前缀或 endpoint 匹配，配置后只对当前进程 cgroup id 命中的目标生效；ptrace、capable、task_fix_setuid、task_fix_setgid 与 task_fix_setgroups 规则只接受 scoped cgroup target，不允许全局阻断。`file_access` 支持 `any/read/write`，默认 `any` 兼容原路径阻断；`write` 模式只匹配 `O_WRONLY/O_RDWR`，可与精确 `path` 或目录 `path_prefix` 组合，用于验证目标 cgroup 内“可读但不可写”的文件或只读目录策略。LSM file/bprm/socket/ptrace/capable/credential 事件会携带 `target_index`，用户态据此还原单条 YAML `rule_id/target_ref`；file_open 事件额外携带 `path_prefix/file_access/file_flags`，socket 事件额外携带 `dst_ip/dst_port/protocol`，exec_prefix 事件额外携带用户态回填的 `exec_prefix`，ptrace 事件携带 `path=ptrace_traceme`，capable 事件携带 capability 编号并由用户态输出为 `CAP_SYS_ADMIN` 等名称，task_fix_setuid 事件携带 `uid/euid/suid/setuid_flags`，task_fix_setgid 事件携带 `gid/egid/sgid/setgid_flags`，task_fix_setgroups 事件携带 `group_count/old_group_count`、`cred_gfp`；tracepoint 观测事件统一写 unknown target，并可被用户态 `anomaly_rules` 复用；当前 `burst_execve` 规则只在用户态做窗口聚合，BPF 热路径不承担复杂异常判断。`bprm_check_security` 优先从 trusted `bprm->file` 解析执行文件路径，若未命中再回退到 `bprm->filename`，以兼容脚本解释器路径和用户态传入路径差异；匹配逻辑同时支持精确 `exec_path` 与字面 `exec_prefix`。当前 `target_map` 已支持多 path/path_prefix/file_access/exec/exec_prefix/socket/ptrace/capability/setuid/setgid/setgroups/cred_prepare 目标和显式 cgroup scope；正式说明和最小测试入口见 `docs/security_policy_skill.md` 与 `tests/integration/test_security_policy.sh`。
