# security_policy Skill

职责：把 EulerPilot 的安全策略能力从 demo 扩展为可审计、可强制、可回滚的运行时 Skill。当前仓库已注册正式 `security_policy`，并保留 `security_policy_demo` 作为兼容回归入口。

## 目标形态

正式 `security_policy` 应支持两种模式：

- `audit`：只记录 syscall/LSM 命中事件，不阻断行为，默认安全模式。
- `enforce`：只对显式 target 和规则命中的行为返回拒绝错误，必须能 rollback。

目标过滤应覆盖：

- 进程：PID/TGID/comm，用于最小调试。
- 路径：文件访问与程序执行路径，最低覆盖 `file_open` 和 `bprm_check_security`。
- 容器：通过 host PID、mount namespace、cgroup 或 container ID 解析目标。
- Kubernetes：后续由 Pod target 解析到 host 侧 namespace/cgroup；最小测试不依赖 Kubernetes。

事件输出应进入统一 AuditBus 或 JSONL，包含 hook、mode、action、target、pid/tgid、comm、path、rule_id、reason、errno 等字段。

## 当前完成度

当前 `security_policy` 的能力边界：

- BPF 程序：`bpf/security_policy_demo.bpf.c`
- Hook：`lsm/file_open`、`lsm/bprm_check_security`、`lsm/socket_connect`、`lsm/ptrace_traceme`、`lsm/capable`、`lsm/task_fix_setuid`、`lsm/task_fix_setgid`、`lsm/task_fix_setgroups`、`lsm/cred_prepare`，以及 `tracepoint/syscalls/sys_enter_execve`、`tracepoint/syscalls/sys_enter_openat`、`tracepoint/syscalls/sys_enter_connect`、`tracepoint/syscalls/sys_enter_ptrace`
- 行为：`lsm/file_open` 可在 enforce 模式拒绝 `target_map` 中的目标文件，并支持 `file_access=any/read/write`；`path_prefix + file_access=write` 可表达只读目录保护；`lsm/bprm_check_security` 可拒绝精确执行路径或字面执行前缀；`lsm/socket_connect` 可按 `dst_ip + dst_port + cgroup_id` 拒绝 scoped IPv4 connect；`lsm/ptrace_traceme`、`lsm/capable`、`lsm/task_fix_setuid`、`lsm/task_fix_setgid`、`lsm/task_fix_setgroups` 与 `lsm/cred_prepare` 都只允许 scoped cgroup target，分别验证 ptrace、CAP_SYS_ADMIN、setuid、setgid、setgroups 与 cred_prepare 阻断；credential 事件分别输出 `uid/euid/suid/setuid_flags`、`gid/egid/sgid/setgid_flags`、`group_count/old_group_count`、`uid/euid/suid/gid/egid/sgid/group_count/old_group_count/cred_gfp`；四类 syscall 当前只做 audit 观测；`anomaly_rules` 当前支持 `burst_execve` 速率规则。
- 用户态：`SecurityPolicyDemoSkill` 同时服务 `security_policy` 和 `security_policy_demo`；正式名读取 YAML v2 `targets + rules + target_ref`，并从最多 8 条规则引用的 `targets.<target_ref>` 解析 path、path_prefix、file_access、exec_path、exec_prefix、dst_ip、dst_port、capability、cgroup_path、PID、container_id、container name 和 k8s_pod 后填充 BPF `target_map`；`lsm_ptrace_traceme`、`lsm_capable`、`lsm_task_fix_setuid`、`lsm_task_fix_setgid`、`lsm_task_fix_setgroups` 与 `lsm_cred_prepare` 要求 target 已解析出 cgroup scope；LSM blocked 事件会根据 BPF `target_index` 映射回单条 YAML `rule_id/target_ref`，并输出 cgroup、endpoint、exec_prefix、file_access、capability 和 credential 证据。
- audit：attach BPF LSM + 四类 syscall tracepoint，但通过 `policy_map.enforce=0` 保持允许，命中后通过 ringbuf 写入 `reports/events/security_policy.jsonl`
- enforce：通过 libbpf 打开 `/root/EulerPilot/build/security_policy_demo.bpf.o` 并 attach LSM + tracepoint；当前九类 LSM 会对 `target_map` 中最多 8 组 path/path_prefix/file_access/exec/exec_prefix/socket/ptrace/capability/setuid/setgid/setgroups/cred_prepare/cgroup scope 目标返回拒绝；未配置 scope 时兼容路径匹配，配置 `cgroup_path`、`type: pid`、`type: container_id`、`type: container` 或 `type: k8s_pod` 后只对解析出的目标 cgroup 生效；ptrace、capable、setuid、setgid、setgroups 与 cred_prepare 规则必须带 scope。
- 回滚：Agent stop/rollback 销毁所有 BPF link 和 object；正常路径不 pin link

当前尚未完成：

- cred_transfer/cred_alloc_blank 等更多 cred 生命周期规则、更多 LSM hook、更多异常规则和进程过滤
- syscall 事件与 Pod/container target 联动处置

## 参考复用

- `third_party/reference/kata-lsm-ebpf`：重点参考 BPF LSM 多 hook、mount namespace profile mode、规则 map、audit ringbuf 和用户态 map 下发；可抽取成 openEuler 版 Security 模块，但不能整体复制。
- `third_party/reference/lmp-xdp-lsm`：参考最小 LSM hook 与 skeleton attach/detach，适合作为装载链路和 hook 形态对照。
- Android PSI 论文代码或 `third_party/reference/perfinsight-psi`：只复用 PSI 门控和实验方法；如要进入 EulerPilot，必须复制到项目目录后按 openEuler 24.03-LTS-SP3 的 BTF/libbpf/x86_64 环境重新编译。

## 最小验证

```bash
sudo tests/integration/test_security_policy.sh
```

脚本会构建 Agent 和 demo BPF 对象，先启动正式 `security_policy` 的 audit 模式，确认目标文件和 demo 可执行文件不被阻断，且写入 `lsm_file_open`、`lsm_bprm_check_security`、`sys_enter_execve`、`sys_enter_openat`、`sys_enter_connect`、`sys_enter_ptrace` 等 BPF ringbuf hit 事件；随后启用 `burst_execve` 异常规则；再启动 enforce 模式，验证目标文件、demo 可执行文件、scoped writable-dir exec_prefix、scoped file_access 写打开、scoped path_prefix 只读目录写打开、scoped IPv4 socket connect、scoped `PTRACE_TRACEME`、scoped `CAP_SYS_ADMIN`、scoped setuid/setgid/setgroups credential 转换和 scoped cred_prepare credential preparation 在策略生效期间被拒绝并写入 blocked hit 事件，并在 Agent 退出后恢复可访问。脚本还会创建动态 `/tmp` 目标和临时 cgroup，验证显式 cgroup、PID、container_id、runtime container name 和 k8s_pod target scope。当前集成脚本仍以 `/root/EulerPilot` 为基准；121 最新通过结果为 `results/security_policy/integration-20260624-114838`，122 最新通过结果为 `results/security_policy/integration-20260624-115440`。

更完整的设计、验收口径和下一步清单见 `docs/security_policy_skill.md`。

## v3.1 anomaly 规则

v3.1 新增三条服务联动 anomaly：

- `burst_connect`
- `burst_openat_sensitive`
- `capability_abuse`

默认用于第二条 Policy Engine 联动的是 `burst_connect`。该规则复用 connect 相关 syscall/LSM 事件，不额外扩大 hook 面，事件进入 `reports/events/security_policy.jsonl` 后由 `policy_engine` 消费。
