# SecurityPolicySkill 正式化说明

本文面向比赛评委和后续 Agent，说明 EulerPilot Security Agent 的能力定位、当前完成度、参考代码复用边界和最小验收入口。当前仓库已经注册正式 `security_policy` Skill，并保留 `security_policy_demo` 兼容入口。正式入口已支持 YAML v2 `targets + rules + target_ref`，`audit` 与 `enforce` 都会 attach 最小 BPF LSM 程序并消费 ringbuf 命中事件；audit 路径已补入 `sys_enter_execve`、`sys_enter_openat`、`sys_enter_connect` 与 `sys_enter_ptrace` tracepoint 观测事件。2026-06-22 已补真实 `lsm/socket_connect`、bprm `exec_prefix` 和 file_open `file_access=write`；2026-06-23 已补 `lsm/ptrace_traceme`、`lsm/capable`、`lsm/task_fix_setuid`、`lsm/task_fix_setgid`、`lsm/task_fix_setgroups`、`path_prefix + file_access=write` 只读目录保护和 `burst_execve` 用户态异常规则；2026-06-24 已补 `lsm/cred_prepare` scoped credential preparation 阻断；2026-06-29 已补 `policy_engine` 联动验证，`burst_execve` anomaly 可触发 Resource Control cgroup 降级并在 Agent 退出后回滚；2026-07-03 已补服务联动 anomaly 规则验收，`burst_connect`、`burst_openat_sensitive` 与 `capability_abuse` 已在 121/122 双机通过。blocked 事件会输出 endpoint、exec_prefix、file_access、path_prefix、ptrace、capability 和 credential 证据；其中 cred_prepare 事件输出 `uid/euid/suid/gid/egid/sgid/group_count/old_group_count/cred_gfp`、`cgroup_id/cgroup_path` 与规则标识。`audit` 模式通过 map 配置 `enforce=0`，只记录命中、不阻断；`enforce` 模式通过 `enforce=1` 对当前 YAML 声明目标执行拒绝，并在 Agent 退出时通过 BPF link fd 自动 detach。

## 能力定位

SecurityPolicySkill 的正式目标是把安全策略作为 EulerPilot 的运行时 Skill 接入统一控制面，支持 `audit` 和 `enforce` 两种模式。

- `audit` 模式：只观测并输出事件，不改变内核安全决策。该模式用于评估策略命中面、生成候选规则、验证 target 解析是否准确，应作为默认安全模式。
- `enforce` 模式：在明确 target 与规则后，由 BPF LSM hook 返回 `-EPERM` 或 `-EACCES` 等错误码阻断命中行为。该模式只能对显式选择的进程、容器、cgroup 或路径生效，不能作为全局默认。

BPF LSM 的边界需要写清楚：它运行在内核 LSM hook 链中，只能在已有 LSM 结果未拒绝时追加拒绝，不能覆盖前序 LSM 的拒绝结果；它要求目标内核启用 BPF LSM、挂载 bpffs，并以 root 或等价能力运行加载器。正式 Skill 必须先做能力探测，再决定是否进入 audit/enforce。

## Target 与过滤模型

正式实现应复用公共 `TargetResolver` 思路，把策略作用域从 demo 固定路径扩展为可声明 target：

- 进程过滤：PID/TGID、comm、父子进程关系，只用于短期调试或 demo。
- 路径过滤：文件路径、可执行路径、前缀/后缀/精确匹配，最低覆盖 `file_open` 与 `bprm_check_security`。
- 容器过滤：优先通过 host PID 解析 mount namespace、cgroup path、容器 ID，再把规则落到 mount namespace 或 cgroup 维度。
- Kubernetes 过滤：正式能力可以由 Pod/container target 解析到 host PID、cgroup、namespace；最小 integration 测试不依赖 Kubernetes。

当前 `bpf/security_policy_demo.bpf.c` 的强制控制已经从 BPF 硬编码路径切到用户态填充的 `target_map`。当前 BPF map 最多支持 8 组文件路径、文件访问权限、精确执行路径、执行路径前缀、IPv4 socket endpoint、scope-only ptrace/setuid/setgid/setgroups cgroup target 和 capability target；默认 demo 配置仍指向：

```text
/root/EulerPilot/demo/security_policy_demo/secret.txt
/root/EulerPilot/demo/security_policy_demo/deny_exec.sh
```

它已经有最小 `policy_map`、最多 8 项 `target_map` 和 ringbuf 命中事件。当前 `mode` 已由 BPF map 生效：`audit` attach BPF 但返回 allow；`enforce` attach BPF LSM 并触发 YAML 文件路径、文件前缀、文件访问权限、精确可执行路径、执行路径前缀、socket endpoint、scoped `PTRACE_TRACEME`、scoped capability、scoped setuid/setgid/setgroups credential 转换和 scoped cred_prepare credential preparation 拒绝。`file_access` 支持 `any/read/write`；`path_prefix + file_access=write` 用于表达只读目录保护。`target_map` 每项还可带 `cgroup_id`；`lsm_ptrace_traceme`、`lsm_capable`、`lsm_task_fix_setuid`、`lsm_task_fix_setgid`、`lsm_task_fix_setgroups` 和 `lsm_cred_prepare` 要求 target 必须解析出 cgroup scope，不允许无 scope 全局阻断；`lsm_capable` 额外要求 `capability` / `cap` 字段。配置 `type: pid/container_id/container/k8s_pod` 时，用户态会通过 `TargetResolver` 解析到 cgroup scope 后写入 BPF。当前 tracepoint 观测覆盖 `sys_enter_execve`、`sys_enter_openat`、`sys_enter_connect` 与 `sys_enter_ptrace`，只做观测和证据输出，不参与 enforce；`anomaly_rules` 已支持 `burst_execve`、`burst_connect`、`burst_openat_sensitive` 和 `capability_abuse`，由用户态按 `threshold/window_ms` 聚合。文档和验收时应把当前阶段称为正式 `security_policy` file_open path/path_prefix/access + bprm exact/prefix + socket_connect + ptrace_traceme + capable + task_fix_setuid + task_fix_setgid + task_fix_setgroups + cred_prepare + 四类 syscall + 多目标 target_map + cgroup/pid/container/runtime/pod scope + runtime anomaly 最小闭环，而不是完整 SecurityPolicySkill 成品。复杂规则矩阵、cred_transfer/cred_alloc_blank 等更多 cred 生命周期规则和更多 LSM hook 仍未完成。

## 事件输出

正式事件应进入统一 `AuditBus` 或等价 JSONL 文件，建议字段包括：

- `ts_ns`、`skill=security_policy`、`mode`、`action=audit|deny|allow`
- `hook=file_open|bprm_check_security|socket_connect|ptrace_traceme|capable|task_fix_setuid|task_fix_setgid|task_fix_setgroups|cred_prepare|execve|openat|connect|ptrace`
- `target_ref`、`pid`、`tgid`、`comm`、`cgroup`、`mnt_ns`
- `path` 或 `exec_path`、`errno`、`rule_id`、`reason`

当前 BPF demo 已输出 ringbuf 命中事件；用户态 `security_policy` 消费事件并写入 `reports/events/security_policy.jsonl`。当前事件已区分 `event_hook=lsm_file_open`、`event_hook=lsm_bprm_check_security`、`event_hook=lsm_socket_connect`、`event_hook=lsm_ptrace_traceme`、`event_hook=lsm_capable`、`event_hook=lsm_task_fix_setuid`、`event_hook=lsm_task_fix_setgid`、`event_hook=lsm_task_fix_setgroups`、`event_hook=lsm_cred_prepare`、`event_hook=sys_enter_execve`、`event_hook=sys_enter_openat`、`event_hook=sys_enter_connect` 和 `event_hook=sys_enter_ptrace`。LSM blocked 事件已携带 BPF `target_index`，用户态会映射回 YAML 中的单条 `rule_id` 和 `target_ref`；scoped target 命中时还会输出 `cgroup_id/cgroup_path`。socket 事件输出 `dst_ip/dst_port/protocol`，exec_prefix 事件输出 `exec_prefix`，file_open 权限维度事件输出 `path_prefix/file_access/file_flags`，ptrace 事件输出 `path=ptrace_traceme`，capable 事件输出 `capability`，task_fix_setuid 事件输出 `uid/euid/suid/setuid_flags`，task_fix_setgid 事件输出 `gid/egid/sgid/setgid_flags`，task_fix_setgroups 事件输出 `group_count/old_group_count`，cred_prepare 事件输出 `uid/euid/suid/gid/egid/sgid/group_count/old_group_count/cred_gfp`。tracepoint 观测事件不参与 LSM enforce 规则匹配，统一写 `target_index=unknown`。`anomaly_rules` 已覆盖 `burst_execve`、`burst_connect`、`burst_openat_sensitive` 和 `capability_abuse`；其中 `burst_execve` 已作为第一条 Policy Engine 触发源，`burst_connect` 已作为第二条和真实 Pod 联动触发源。最小验收以 audit 不阻断、九类 LSM scoped enforce、四类 syscall 观测、多目标 target_map、规则级 blocked 事件、cgroup/PID/container/runtime/Pod target scope、Agent 退出后恢复访问、无 BPF link/pin 残留和 anomaly 联动证据为准。下一步成品化重点转向 cred_transfer/cred_alloc_blank 等更多 cred 生命周期规则、更多异常策略组合和联动处置。

## 回滚与清理

SecurityPolicySkill 涉及内核安全决策，默认回滚语义必须保守：

- BPF link 默认不 pin，Agent 退出或 rollback 时关闭 fd 即 detach。
- enforce 启动失败时立即销毁 link/object，并保留明确错误。
- 清理入口保留 `scripts/cleanup_security_policy_demo.sh`，用于删除 demo 相关 pin；当前 demo 正常路径不应留下 pin。
- integration 测试失败时必须 kill Agent、调用 cleanup、验证目标文件访问恢复。

## 参考代码复用边界

`third_party/reference/kata-lsm-ebpf` 是 SecurityPolicySkill 最接近正式化的参考快照，但不能整体并入生产路径。建议抽取为 openEuler 模块的部分：

- `varmor_lsm/varmor_lsm.bpf.c`：多 LSM hook 组织、mount namespace 级 profile mode、file/bprm/network/ptrace/mount 规则 map、ringbuf audit 事件。
- `varmor_lsm/varmor_lsm.c`：用户态加载、map 更新、profile/rule 下发、audit ringbuf 读取、link 生命周期管理。
- `varmor_lsm/apply_lsm_policy.c`：最小 CLI 的运行方式、信号退出、清理旧规则和 detach 流程。
- `kata_lsm_agent/kata_lsm_agent.bpf.c`：较小的 exec 路径 deny map、control map、ringbuf 事件结构，适合作为第一版 bprm enforce/audit 的轻量模板。

`third_party/reference/lmp-xdp-lsm` 与 `third_party/reference/lmp` 只作为 LSM hook 和 libbpf skeleton 装载方式参考：

- `lsm/lsm-connect/` 可参考最小 `lsm/socket_connect` 阻断样例。
- `lsm/lsm_bpf_monitoring/` 可参考 `bprm_check_security`、`file_open`、`cred_prepare` 等 hook 分布和 skeleton attach/detach。
- 不应复制其中旧项目的宽泛框架、硬编码 PID/文件名或不完整 verifier 处理。

用户 Android PSI 论文代码和 `third_party/reference/perfinsight-psi` 更适合复用“观测门控”和实验方法：用 PSI 作为高压力窗口筛选信号，减少低压状态下的安全事件噪声。Android 侧源码或二进制不能直接用于 openEuler；如要并入 EulerPilot，必须先复制到项目目录或 reference snapshot，删除 Android 设备/ARM64/ADB 绑定，再按 openEuler 24.03-LTS-SP3 的内核头、BTF、libbpf 和 x86_64 工具链重新编译验证。

## 最小验证入口

不依赖 Kubernetes 的验证入口：

```bash
sudo tests/integration/test_security_policy.sh
sudo tests/integration/test_security_policy_anomaly_rules.sh
```

脚本默认只验证当前 demo 能力：

1. 检查默认 demo 目标位于 `/root/EulerPilot`，因为当前集成脚本仍以该路径作为 demo 配置和结果目录基准。
2. 检查 root、BPF LSM、bpffs、`bpftool`、`make`、`timeout` 等基础命令。
3. 执行 `make agent security-policy-demo`。
4. 使用临时 audit 配置启用正式 `security_policy`，确认目标文件和 demo 可执行文件仍可访问，且写入 BPF ringbuf 命中事件，并覆盖 `lsm_file_open`、`lsm_bprm_check_security`、`sys_enter_execve`、`sys_enter_openat`、`sys_enter_connect`、`sys_enter_ptrace`。
5. 使用临时 anomaly 配置启用 `anomaly_rules: burst_execve`，连续执行系统 `true` 二进制，确认 `security_policy_events.anomaly-execve.jsonl` 包含 `operation=anomaly`、`rule_id=burst_execve`、`event_hook=sys_enter_execve`、`threshold/window_ms`。
5.1. 使用 `test_security_policy_anomaly_rules.sh` 启用服务联动 anomaly 配置，触发本地 connect burst、`/etc` openat burst 和 scoped `CAP_SYS_ADMIN` capable burst，确认 `security_policy_events.anomaly-rules.jsonl` 包含 `burst_connect`、`burst_openat_sensitive`、`capability_abuse` 三类 anomaly。
6. 使用临时 enforce 配置启用正式 `security_policy`，轮询读取 demo secret 文件并执行 demo deny 脚本，确认 attach 后均被拒绝。
7. 再创建 `/tmp/eulerpilot-security-policy.*` 下两组动态文件和动态执行脚本，使用临时 YAML 把两组 `path` 与 `exec_path` 下发到 `target_map`，确认两个动态目标都被拒绝，blocked 事件分别带上对应的 `rule_id/target_ref`，同时原 demo 目标不被误阻断。
8. 创建临时 cgroup，并使用带 `cgroup_path` 的 target 验证同一敏感文件在目标 cgroup 外可访问、在目标 cgroup 内被拒绝，事件带上 `cgroup_id/cgroup_path`。
9. 创建一个位于目标 cgroup 内的临时 PID，并使用 `type: pid` target 验证用户态能自动从 PID 解析 cgroup scope，且 scope 外访问成功、scope 内访问拒绝。
10. 启动本地 TCP server，并使用 `hook: lsm_socket_connect`、`type: cgroup`、`dst_ip: 127.0.0.1`、`dst_port` 验证目标 cgroup 内 IPv4 connect 被 LSM 拒绝，scope 外 connect 成功，blocked 事件携带 `dst_ip/dst_port/protocol/cgroup_id`。
11. 创建 `/tmp/eulerpilot-security-policy.*` 下的可执行脚本，并使用 `hook: lsm_bprm_check_security`、`type: cgroup`、`exec_prefix` 验证目标 cgroup 内可写目录前缀执行被拒绝，scope 外执行成功，blocked 事件携带 `exec_prefix/cgroup_id`。
12. 创建 `/tmp/eulerpilot-security-policy.*` 下的普通文件，并使用 `hook: lsm_file_open`、`type: cgroup`、`path`、`file_access: write` 验证目标 cgroup 内读打开继续成功、写打开被拒绝，scope 外写入成功，blocked 事件携带 `file_access=write/file_flags/cgroup_id`。
13. 创建 `/tmp/eulerpilot-security-policy.*/read-only-dir/` 目录，并使用 `hook: lsm_file_open`、`type: cgroup`、`path_prefix`、`file_access: write` 验证目标 cgroup 内目录前缀下文件读打开继续成功、写打开被拒绝，scope 外写入成功，blocked 事件携带 `path_prefix/file_access=write/file_flags/cgroup_id`。
14. 创建 `type: cgroup` 的 scope-only target，并使用 `hook: lsm_ptrace_traceme` 验证目标 cgroup 内 `PTRACE_TRACEME` 被拒绝、scope 外允许，blocked 事件携带 `event_hook=lsm_ptrace_traceme`、`path=ptrace_traceme` 和 `cgroup_id/cgroup_path`。
15. 创建 `type: cgroup` target，并使用 `hook: lsm_capable` + `capability: CAP_SYS_ADMIN` 验证目标 cgroup 内 `unshare -m true` 被拒绝、scope 外允许，blocked 事件携带 `event_hook=lsm_capable`、`capability=CAP_SYS_ADMIN` 和 `cgroup_id/cgroup_path`。
16. 创建 type=cgroup 的 scope-only target，并使用 `hook: lsm_task_fix_setuid` 验证目标 cgroup 内 `setuid(65534)` 被拒绝、scope 外允许，blocked 事件携带 `event_hook=lsm_task_fix_setuid`、`uid/euid/suid/setuid_flags` 和 `cgroup_id/cgroup_path`。
17. 创建 type=cgroup 的 scope-only target，并使用 `hook: lsm_task_fix_setgid` 验证目标 cgroup 内 `setgid(65534)` 被拒绝、scope 外允许，blocked 事件携带 `event_hook=lsm_task_fix_setgid`、`gid/egid/sgid/setgid_flags` 和 `cgroup_id/cgroup_path`。
18. 创建 type=cgroup 的 scope-only target，并使用 `hook: lsm_task_fix_setgroups` 验证目标 cgroup 内 `setgroups([65534])` 被拒绝、scope 外允许，blocked 事件携带 `event_hook=lsm_task_fix_setgroups`、`group_count/old_group_count` 和 `cgroup_id/cgroup_path`。
19. 创建 type=cgroup 的 scope-only target，并使用 `hook: lsm_cred_prepare` 验证目标 cgroup 内 credential preparation 被拒绝、scope 外允许，blocked 事件携带 `event_hook=lsm_cred_prepare`、`uid/euid/suid/gid/egid/sgid/group_count/old_group_count/cred_gfp` 和 `cgroup_id/cgroup_path`。
20. 创建带 container ID 字符串的临时 cgroup，并使用 `type: container_id` target 验证用户态能在限定 `cgroup_root` 下解析 cgroup scope，且 scope 外访问成功、scope 内访问拒绝。
21. 创建 fake `crictl` 命令和带 container ID 的临时 cgroup，并使用 `type: container` + `container_name` 验证 runtime CLI 解析路径。
22. 创建 fake `kubectl` 命令和带 Pod UID 的临时 cgroup，并使用 `type: k8s_pod` + `namespace/pod_name` 验证 Pod 名称解析路径。
23. 等 Agent 正常退出，确认目标文件、demo 可执行文件、exec_prefix 目标、file_access 目标、path_prefix 目标、ptrace_traceme、capable、task_fix_setuid、task_fix_setgid、task_fix_setgroups、cred_prepare 和 socket connect 恢复可访问，并调用 cleanup 验证无 demo 残留。

验收输出应包含 `PASS: security_policy audit mode writes file, bprm and four syscall hit events`、`PASS: security_policy detects configurable burst_execve anomaly`、`PASS: security_policy observes burst_connect, burst_openat_sensitive and capability_abuse anomalies`、`PASS: security_policy enforce mode writes blocked file and bprm hit events`、`PASS: target file is denied while security_policy enforce is active`、`PASS: exec target is denied while security_policy enforce is active`、`PASS: security_policy target_map reports rule-specific dynamic YAML file and exec hits`、`PASS: security_policy cgroup scoped target only blocks inside target cgroup`、`PASS: security_policy lsm_socket_connect blocks scoped IPv4 target and reports endpoint evidence`、`PASS: security_policy bprm exec_prefix blocks writable-dir execution inside target cgroup`、`PASS: security_policy file_access=write only blocks write opens inside target cgroup`、`PASS: security_policy path_prefix read-only directory blocks scoped writes`、`PASS: security_policy ptrace_traceme blocks scoped ptrace in target cgroup`、`PASS: security_policy lsm_capable blocks scoped CAP_SYS_ADMIN in target cgroup`、`PASS: security_policy lsm_task_fix_setuid blocks scoped setuid transitions`、`PASS: security_policy lsm_task_fix_setgid blocks scoped setgid transitions`、`PASS: security_policy lsm_task_fix_setgroups blocks scoped setgroups transitions`、`PASS: security_policy lsm_cred_prepare blocks scoped credential preparation`、`PASS: security_policy pid target resolves to cgroup scoped enforcement`、`PASS: security_policy container_id target resolves to cgroup scoped enforcement`、`PASS: security_policy container runtime name target resolves to cgroup scoped enforcement`、`PASS: security_policy k8s pod name target resolves to cgroup scoped enforcement` 和 rollback 恢复类 PASS。

## 正式实现清单

- 注册正式 `security_policy` Skill，保留 `security_policy_demo` 作为回归用例。（已完成）
- YAML schema v2 支持 `targets + rules + mode + target_ref`，默认 `mode: audit`。（已完成最小路径 target）
- 用户态 audit/enforce 模式切换：audit 不阻断并写事件，enforce 执行 BPF LSM。（已完成最小闭环）
- BPF 侧加入最小 config map 和 event ringbuf。（已完成 demo target、LSM file_open、LSM bprm_check_security、LSM socket_connect、LSM ptrace_traceme、LSM capable、LSM task_fix_setuid、LSM task_fix_setgid、LSM task_fix_setgroups、LSM cred_prepare、execve/openat/connect/ptrace tracepoint 事件）
- BPF 侧加入动态 map：target map、path rule map、exec rule map、control map。（已完成最多 8 组 path/path_prefix/file_access/exec/exec_prefix/socket/ptrace/capability/setuid/setgid/setgroups/cred_prepare/cgroup scope `target_map`，用户态从 YAML target path、path_prefix、file_access、exec path、exec_prefix、dst_ip/dst_port、capability、可选 cgroup_path、`type: pid`、`type: container_id`、`type: container` 和 `type: k8s_pod` 解析结果填充；LSM file/bprm/socket/ptrace/capable/credential blocked 事件已支持规则级标识）
- 最低覆盖 `execve/openat/connect/ptrace` syscall tracing，以及 `file_open`、`bprm_check_security`、`socket_connect`、`ptrace_traceme`、`capable`、`task_fix_setuid`、`task_fix_setgid`、`task_fix_setgroups` 和 `cred_prepare` 九类 BPF LSM enforce。（已完成四类 syscall 观测、`file_open` path/file_access enforce、`bprm_check_security` target_map exec path/exec_prefix enforce、`socket_connect` scoped IPv4 endpoint enforce、`ptrace_traceme` scoped cgroup enforce、`capable` scoped capability enforce、`task_fix_setuid` scoped setuid credential enforce、`task_fix_setgid` scoped setgid credential enforce、`task_fix_setgroups` scoped supplementary groups enforce、`cred_prepare` scoped credential preparation enforce，以及 `burst_execve/burst_connect/burst_openat_sensitive/capability_abuse` anomaly 规则）
- 用户态接入 `TargetResolver`、`AuditBus`、`ActionJournal` 和 `CapabilityDetector`。
- integration 分层：本地 demo、container namespace、Pod target；Pod 测试只作为增强，不作为最小入口。
- 文档和脚本明确 openEuler 24.03-LTS-SP3 为硬性目标环境，其他发行版只作为兼容性参考。

## 验收口径

当前可验收：正式 `security_policy` 注册名、YAML v2 path/path_prefix/file_access/exec_path/exec_prefix/dst_ip/dst_port/capability/cgroup_path/pid/container_id/container/k8s_pod target、最多 8 组 BPF `target_map`、audit 不阻断并写 BPF hit event、四类 syscall tracing、`burst_execve/burst_connect/burst_openat_sensitive/capability_abuse` anomaly event、九类 LSM enforce、规则级 blocked 事件、scoped IPv4 socket connect、exec_prefix、file_access、path_prefix、ptrace_traceme、CAP_SYS_ADMIN、setuid/setgid/setgroups credential 转换、cred_prepare credential preparation、显式 cgroup/PID/container_id/runtime container/k8s_pod scope、Agent 退出恢复和 cleanup 无残留，以及 `policy_engine` 消费 `burst_execve/burst_connect` anomaly 后触发 Resource Control 或 Network+Resource 联动。Security 121 最新基础结果目录为 `results/security_policy/integration-20260624-114838`，122 为 `results/security_policy/integration-20260624-115440`；服务联动 anomaly 121 结果目录为 `results/security_policy/anomaly-rules-20260703-121-v4`，122 为 `results/security_policy/anomaly-rules-20260703-122-v2`；联动验证 121 结果目录为 `results/policy_engine/security-resource-20260629-163949` 和 `results/policy_engine/security-network-resource-20260629-214952`，122 结果目录为 `results/policy_engine/security-resource-20260629-164135` 和 `results/policy_engine/security-network-resource-20260629-215950`。121 最新质量门禁为 `reports/final_quality_gate_20260630-v32-real-pod-policy-121.log`。

下一阶段正式验收：在已完成 `lsm/socket_connect`、bprm `exec_prefix`、file_open `file_access/path_prefix`、`lsm/ptrace_traceme`、`lsm/capable`、`lsm/task_fix_setuid`、`lsm/task_fix_setgid`、`lsm/task_fix_setgroups`、`lsm/cred_prepare` 和三类服务联动 anomaly 的基础上，继续补 cred_transfer/cred_alloc_blank 等更多 cred 生命周期规则、更多异常策略组合和联动处置。所有新增 hook 都必须复用当前 target/cgroup scope，不要重新实现 Pod/container 解析。

## v3.1 anomaly 增强

v3.1 为服务联动补充了三类小范围 anomaly 规则，不新增大规模 LSM hook，优先复用已有 syscall/LSM/ringbuf 事件：

- `burst_connect`：短时间大量 `connect` 或命中敏感端口，用于触发 `security_policy -> policy_engine -> resource_control + network_qos` 第二条联动。
- `burst_openat_sensitive`：短时间频繁访问 `/proc/sys`、`/etc`、`/root` 等敏感路径。
- `capability_abuse`：目标 cgroup 内频繁触发 capability/credential 相关事件。

这些规则继续复用 `target_ref/cgroup scope`，事件写入 `reports/events/security_policy.jsonl`。其中 `burst_connect` 是 v3.1 默认触发源，要求 anomaly 事件在触发后 1 秒内可见，并携带可被 Policy Engine 读取的 `event_id`，作为后续 `trigger_event_id`。

双机证据：

- 121：`results/security_policy/anomaly-rules-20260703-121-v4`
- 122：`results/security_policy/anomaly-rules-20260703-122-v2`

结果目录中的 `security_policy_events.anomaly-rules.jsonl` 只保留 `operation=anomaly` 事件；完整运行日志保留在 `agent.log`，用于排查但不作为默认展示入口。
