# EulerPilot Skills 与 YAML 能力规划 v2.1

更新时间：`2026-07-17`

## 1. 当前判断

`Skill / SkillRegistry / SkillManager / builtin_skills` 已经完成第一阶段闭环，`resource_control / psi_gate / network_policy / network_qos / network_xdp / security_policy / network_policy_demo / security_policy_demo` 也已进入统一 Agent 管理。

当前 `configs/skills.yaml` 已升级为 `schema_version: 2`。解析层通过 flatten 嵌套 YAML 的方式兼容现有 `SkillSpec.config`，因此旧 `schema_version: 1` flat config 仍可使用。

下一阶段不再是“证明可以扩展 Skill”，而是把 demo 级能力升级为正式 Agent 能力：

```text
统一 Skill 接口
  -> YAML 配置驱动
  -> 公共目标解析
  -> 公共能力探测
  -> 公共审计事件
  -> 公共动作日志
  -> 独立 Skill 执行
  -> 跨 Agent 联动
  -> 可量化验收
```

## 2. 命名与口径

历史名称可以在代码迁移期保留，但最终文档、CLI 和质量门禁应逐步去掉 demo 口径。

| 迁移期名称 | 最终口径 |
|------------|----------|
| `network_policy_demo` | `network_policy` |
| `security_policy_demo` | `security_policy` |
| `demo_network_policy_*` | `network_policy_*` 或 `demo_network_policy_product.sh` |
| 单端口 deny | NetworkPolicySkill 的 connect4 子能力 |
| 单路径 deny | SecurityPolicySkill 的 LSM 子能力 |

## 3. 公共基础模块

### 3.1 TargetResolver

所有 Skill 共用目标解析，不允许各自硬编码 PID、cgroup 或网卡。

需要支持：

```text
PID
  -> cgroup path / cgroup id
  -> container id
  -> Pod UID
  -> namespace / Pod name
  -> netns / veth / host interface
```

第一阶段最低实现：

- `pid -> cgroup path`
- `cgroup path -> cgroup id`
- `container_id -> cgroup path / cgroup id`，支持限定 `cgroup_root` 扫描匹配 container ID 字符串
- `container_name -> container_id -> cgroup path / cgroup id`，当前优先支持 `crictl/docker/podman` CLI 解析，集成测试用 fake `crictl` 固定最小闭环
- `k8s_pod namespace/name -> Pod UID -> cgroup path / cgroup id`，当前通过 `kubectl` 查询 Pod UID，集成测试用 fake `kubectl` 固定最小闭环；真实 Pod target 演示脚本已补齐，待 `kubectl + eulerpilot-lab` 环境可用即可转为 pass
- `k8s pod -> cgroup path`
- `k8s pod -> veth/ifindex`
- 拒绝解析到非 `eulerpilot-lab` 的 enforce 目标，除非显式 override

### 3.2 CapabilityDetector

统一输出环境能力：

- BTF
- ring buffer
- BPF LSM
- XDP native/generic
- TC
- cgroup v2 控制器
- PSI
- sched_ext
- `memory.reclaim`
- Kubernetes/cgroup driver

建议 CLI：

```bash
./build/eulerpilot-agent --doctor-skills --config configs/agent.yaml
./build/eulerpilot-agent --capabilities --json
```

### 3.3 AuditBus

所有 Skill 输出统一事件字段。每个 Skill 可以独立写文件，但 JSON schema 必须一致。

```json
{
  "timestamp": "",
  "event_id": "",
  "skill": "",
  "policy_id": "",
  "rule_id": "",
  "mode": "audit",
  "target": {
    "pid": 0,
    "cgroup_id": 0,
    "container_id": "",
    "namespace": "",
    "pod": ""
  },
  "operation": "",
  "evidence": {},
  "action": "",
  "result": "",
  "severity": "info"
}
```

### 3.4 ActionJournal

所有有副作用的 Skill 都必须记录可回滚信息。

建议路径：

```text
run/eulerpilot/action_journal.json
```

记录内容：

- cgroup 原值与新值
- BPF link
- pinned map
- qdisc/class/filter
- XDP interface
- policy id / rule id
- 是否已恢复

Agent 停止、异常恢复和下次启动时都应能根据 Journal 清理残留。

## 4. Skill 分类

### 4.1 RuntimeSkill

进入 Agent 生命周期：

- `cpu_scheduling`
- `resource_control`
- `psi_gate`
- `network_policy`
- `security_policy`

### 4.2 ToolSkill

不进入热路径，但需要可枚举：

- `benchmark`
- `report`
- `dashboard`
- `release_check`

## 5. 顶层 YAML 模型

建议统一到 `configs/agent.yaml`，或由 `agent.yaml` 引入 `skills.yaml`。

```yaml
schema_version: 2

agent:
  mode: audit          # observe | audit | enforce
  lab_namespace: eulerpilot-lab
  audit_dir: reports/events
  journal_path: run/eulerpilot/action_journal.json

targets:
  redis_cgroup:
    type: cgroup
    path: /sys/fs/cgroup/eulerpilot/demo-redis

  web_pod:
    type: k8s_pod
    namespace: eulerpilot-lab
    name: web-demo

  xdp_veth:
    type: netdev
    ifname: ep-veth-xdp

skills:
  resource_control:
    enabled: true

  psi_gate:
    enabled: true

  network_policy:
    enabled: false

  security_policy:
    enabled: false
```

模式语义：

| 模式 | 含义 |
|------|------|
| `observe` | 只采集状态，不判定策略，不执行动作 |
| `audit` | 规则判定并记录事件，但不阻断或限额 |
| `enforce` | 执行策略动作，必须写 ActionJournal |

历史 `dry-run` 可作为 `audit` 的兼容别名，但最终文档建议统一使用 `audit`。

## 6. NetworkPolicySkill YAML

Network 必须使用“目标定义 + 规则引用”，不能让 `cgroup/connect4`、TC 和 XDP 共享一个全局 cgroup target。当前已落地 `cgroup_connect4`、`tc_egress` 与 isolated-veth `xdp` 的 v2 最小闭环。

```yaml
network_policy:
  enabled: false
  mode: audit

  targets:
    redis_cgroup:
      type: cgroup
      path: /sys/fs/cgroup/eulerpilot/demo-net

    lab_veth:
      type: netdev
      ifname: ep-veth-xdp

    web_pod:
      type: k8s_pod
      namespace: eulerpilot-lab
      name: web-demo

  rules:
    - name: deny_redis_port
      hook: cgroup_connect4
      target_ref: redis_cgroup
      protocol: tcp
      dst_port: 6379
      action: deny

    - name: limit_http_egress
      hook: tc_egress
      target_ref: web_pod
      dst_port: 18080
      rate: 5mbit
      action: limit

    - name: xdp_drop_udp
      hook: xdp
      target_ref: lab_veth
      protocol: udp
      dst_port: 9999
      action: drop

    - name: xdp_drop_udp_tuple
      hook: xdp
      target_ref: lab_veth
      protocol: udp
      src_ip: 10.89.0.2
      dst_ip: 10.89.0.1
      src_port: 39094
      dst_port: 19094
      action: drop
```

Hook 作用域：

| Hook | 目标要求 |
|------|----------|
| `cgroup_connect4` | `target_ref` 必须解析为 cgroup |
| `tc_ingress/egress` | `target_ref` 必须解析为 netdev、veth 或 pod veth |
| `xdp` | `target_ref` 必须解析为 netdev，且默认只允许 lab veth |

QoS 路线固定为：

```text
eBPF TC classifier
  -> 命中统计
  -> TBF root qdisc
  -> Agent 负责配置与回滚
```

当前实现边界：

- `network_policy` 会选择 `rules.*.hook=cgroup_connect4` 的第一条规则，并解析其 `target_ref` 指向的 `type: cgroup` target。
- `network_qos` 会选择 `rules.*.hook=tc_egress` 的第一条规则，并解析其 `target_ref` 指向的 `type: netdev`、`type: container` 或 `type: k8s_pod` target；container/Pod target 会先解析到 host veth ifname。
- `network_xdp` 会读取最多 8 条 `rules.*.hook=xdp` 规则，并要求这些规则解析到同一个 `type: netdev`、`type: container` 或 `type: k8s_pod` target；container/Pod target 会先解析到 host veth ifname。XDP 规则已支持 `protocol/src_ip/dst_ip/src_port/dst_port`，未配置字段按 wildcard 处理，端口字段仅允许在 TCP/UDP 规则上使用。
- 审计事件已输出 `rule_id` 与 `target_ref`。
- container 和 k8s_pod 到 host veth 解析已经完成 fake runtime/netns 自测和真实 Kubernetes lab Pod 演示；isolated-veth 与 real Pod host veth XDP 均已扩展到 UDP tuple 多字段匹配，后续重点转为 credential 深层 hook 评估和证据压缩。

## 7. SecurityPolicySkill YAML

Security 使用统一目标引用和 observe/audit/enforce 模式。

当前已落地的最小 schema 使用 `targets + rules + target_ref` 描述 path/path_prefix/file_access/exec/exec_prefix/socket/ptrace/capability/setuid/setgid/setgroups/cred lifecycle target，默认 `audit` 且默认 disabled。v2 正式入口对 `lsm_file_open` 规则要求 `targets.<target_ref>.path` 或 `targets.<target_ref>.path_prefix` 至少存在一个，`file_access` 可选为 `any/read/write`，默认 `any` 兼容旧路径阻断；`path_prefix + file_access=write` 用于表达只读目录保护，并可选配置 `cgroup_path` 作为作用域；对 `lsm_bprm_check_security` 规则要求 `exec_path` 或 `exec_prefix` 至少存在一个，并可复用 cgroup/PID/container/Pod 作用域；对 `lsm_socket_connect` 规则要求 `dst_ip` 和 `dst_port` 显式存在，并可复用 `cgroup_path`、PID、container 或 Pod 解析出的 cgroup 作用域；对 `lsm_ptrace_traceme` 规则要求 target 必须解析出 cgroup scope，且不携带 path/exec/socket 字段，避免全局 ptrace deny；对 `lsm_capable` 规则要求 target 必须解析出 cgroup scope，并配置 `capability` / `cap`，避免全局 capability deny；对 `lsm_task_fix_setuid`、`lsm_task_fix_setgid`、`lsm_task_fix_setgroups`、`lsm_cred_prepare`、`lsm_cred_alloc_blank` 和 `lsm_cred_transfer` 规则要求 target 必须解析出 cgroup scope，避免全局 credential deny；其中 `lsm_cred_transfer` 为 observe-only void hook。如果 target 使用 `type: pid`，则用户态通过 `TargetResolver` 从 PID 自动解析 cgroup scope；如果 target 使用 `type: container_id`，则用户态在限定 `cgroup_root` 下扫描包含 container ID 的 cgroup 目录并解析 cgroup scope；如果 target 使用 `type: container`，则可通过 `container_name` 和 runtime CLI 解析 container ID；如果 target 使用 `type: k8s_pod`，则可通过 `namespace/pod_name` 和 `kubectl` 查询 Pod UID 后解析 cgroup scope。用户态会扫描最多 8 条 `rules.*.target_ref` 并把对应 path/path_prefix/file_access/exec/exec_prefix/socket/ptrace/capability/setuid/setgid/setgroups/cred lifecycle/cgroup id 和 scope-only `hook_type` 写入 BPF `target_map`：

```yaml
- name: security_policy
  kind: runtime
  enabled: false
  config:
    mode: audit
    targets:
      demo_secret:
        type: path
        path: /root/EulerPilot/demo/security_policy_demo/secret.txt
        exec_path: /root/EulerPilot/demo/security_policy_demo/deny_exec.sh
        # 可选：配置后只在该 cgroup 内 enforce，不配置则按路径全局匹配。
        cgroup_path: /sys/fs/cgroup/eulerpilot/security-demo
      pid_secret:
        type: pid
        pid: 12345
        path: /tmp/eulerpilot-security-policy/secret.txt
        exec_path: /tmp/eulerpilot-security-policy/deny_exec.sh
      container_secret:
        type: container_id
        container_id: 7f3a2c1d9e00
        cgroup_root: /sys/fs/cgroup/eulerpilot
        path: /tmp/eulerpilot-security-policy/container-secret.txt
        exec_path: /tmp/eulerpilot-security-policy/container-deny-exec.sh
      runtime_secret:
        type: container
        container_name: web-runtime-demo
        runtime: crictl
        cgroup_root: /sys/fs/cgroup
        path: /tmp/eulerpilot-security-policy/runtime-secret.txt
        exec_path: /tmp/eulerpilot-security-policy/runtime-deny-exec.sh
      pod_secret:
        type: k8s_pod
        namespace: eulerpilot-lab
        pod_name: web-demo
        cgroup_root: /sys/fs/cgroup
        path: /tmp/eulerpilot-security-policy/pod-secret.txt
        exec_path: /tmp/eulerpilot-security-policy/pod-deny-exec.sh
      socket_scope:
        type: cgroup
        cgroup_path: /sys/fs/cgroup/eulerpilot/security-demo
        dst_ip: 127.0.0.1
        dst_port: 19019
      writable_exec:
        type: cgroup
        cgroup_path: /sys/fs/cgroup/eulerpilot/security-demo
        exec_prefix: /tmp/eulerpilot-security-policy.lab/
      write_secret:
        type: cgroup
        cgroup_path: /sys/fs/cgroup/eulerpilot/security-demo
        path: /tmp/eulerpilot-security-policy.lab/write-only-target.txt
        file_access: write
      readonly_dir:
        type: cgroup
        cgroup_path: /sys/fs/cgroup/eulerpilot/security-demo
        path_prefix: /tmp/eulerpilot-security-policy.lab/read-only-dir/
        file_access: write
      ptrace_scope:
        type: cgroup
        cgroup_path: /sys/fs/cgroup/eulerpilot/security-demo
      capable_scope:
        type: cgroup
        cgroup_path: /sys/fs/cgroup/eulerpilot/security-demo
        capability: CAP_SYS_ADMIN
      setuid_scope:
        type: cgroup
        cgroup_path: /sys/fs/cgroup/eulerpilot/security-demo
      setgid_scope:
        type: cgroup
        cgroup_path: /sys/fs/cgroup/eulerpilot/security-demo
      setgroups_scope:
        type: cgroup
        cgroup_path: /sys/fs/cgroup/eulerpilot/security-demo
      credential_deep_scope:
        type: cgroup
        cgroup_path: /sys/fs/cgroup/eulerpilot/security-demo
    rules:
      - name: deny_demo_secret_open
        hook: lsm_file_open
        target_ref: demo_secret
        action: deny
      - name: deny_pid_secret_open
        hook: lsm_file_open
        target_ref: pid_secret
        action: deny
      - name: deny_container_secret_open
        hook: lsm_file_open
        target_ref: container_secret
        action: deny
      - name: deny_runtime_secret_open
        hook: lsm_file_open
        target_ref: runtime_secret
        action: deny
      - name: deny_pod_secret_open
        hook: lsm_file_open
        target_ref: pod_secret
        action: deny
      - name: deny_socket_connect
        hook: lsm_socket_connect
        target_ref: socket_scope
        action: deny
      - name: deny_writable_dir_exec
        hook: lsm_bprm_check_security
        target_ref: writable_exec
        action: deny
      - name: deny_write_open
        hook: lsm_file_open
        target_ref: write_secret
        action: deny
      - name: deny_readonly_dir_write
        hook: lsm_file_open
        target_ref: readonly_dir
        action: deny
      - name: deny_ptrace_traceme
        hook: lsm_ptrace_traceme
        target_ref: ptrace_scope
        action: deny
      - name: deny_cap_sys_admin
        hook: lsm_capable
        target_ref: capable_scope
        action: deny
      - name: deny_setuid_transition
        hook: lsm_task_fix_setuid
        target_ref: setuid_scope
        action: deny
      - name: deny_setgid_transition
        hook: lsm_task_fix_setgid
        target_ref: setgid_scope
        action: deny
      - name: deny_setgroups_transition
        hook: lsm_task_fix_setgroups
        target_ref: setgroups_scope
        action: deny
      - name: observe_cred_alloc_blank
        hook: lsm_cred_alloc_blank
        target_ref: credential_deep_scope
        action: deny
      - name: observe_cred_transfer
        hook: lsm_cred_transfer
        target_ref: credential_deep_scope
        action: deny
```

当前语义：

- `audit` 模式 attach BPF LSM + `execve/openat/connect/ptrace` tracepoint，但通过 `policy_map.enforce=0` 允许目标文件访问和 demo 执行脚本运行，并通过 ringbuf 输出 `result=observed` 命中事件。
- `audit` 事件当前覆盖 `event_hook=lsm_file_open`、`event_hook=lsm_bprm_check_security`、`event_hook=sys_enter_execve`、`event_hook=sys_enter_openat`、`event_hook=sys_enter_connect`、`event_hook=sys_enter_ptrace`；`anomaly_rules` 第一版已支持 `burst_execve`，用户态基于 `sys_enter_execve` 事件按 `threshold/window_ms` 聚合并输出 `operation=anomaly/result=observed`。
- `enforce` 模式复用 `bpf/security_policy.bpf.c`，在 `lsm/file_open` 上按 `target_map.file_path/file_prefix + file_access` 拒绝文件打开，在 `lsm/bprm_check_security` 上拒绝任一 `target_map.exec_path` 或 `target_map.exec_prefix`，在 `lsm/socket_connect` 上拒绝任一 `target_map.connect_daddr/connect_dport`，在 `lsm/ptrace_traceme` 上拒绝 scope-only cgroup target 内的 `PTRACE_TRACEME`，在 `lsm/capable` 上拒绝 scoped cgroup target 内的指定 capability，在 `lsm/task_fix_setuid`、`lsm/task_fix_setgid`、`lsm/task_fix_setgroups` 与 `lsm/cred_prepare` 上拒绝 scoped cgroup target 内的 credential 相关动作，并通过 ringbuf 输出 `result=blocked` 命中事件；`lsm/cred_alloc_blank` 已支持 scoped attach 和 hit 输出，`lsm/cred_transfer` 已支持 scoped attach 和 observe-only hit 输出；LSM blocked/hit 事件携带 BPF `target_index`，用户态映射回单条 YAML `rule_id/target_ref`；当 target 配置 `cgroup_path`、`type: pid`、`type: container_id`、`type: container` 或 `type: k8s_pod` 时，BPF 还要求当前进程 cgroup id 命中；scope-only 规则额外校验 `hook_type`，避免同 cgroup 多 credential hook 串错；四类 syscall 当前只做观测，不阻断。
- `tests/integration/test_security_policy.sh` 已额外创建 `/tmp/eulerpilot-security-policy.*` 下两组动态目标，证明 YAML `path/exec_path` 能驱动多目标 BPF `target_map`，blocked 事件能定位到对应规则，且不会误阻断默认 demo 目标；同时创建临时 cgroup 验证带 `cgroup_path` 的目标只在目标 cgroup 内阻断，验证 `type: pid`、`type: container_id`、`type: container` 和 `type: k8s_pod` target 均可解析到 cgroup scope；并启动本地 TCP server 验证 `lsm_socket_connect` 能阻断目标 cgroup 内的 IPv4 endpoint，事件携带 `dst_ip/dst_port/protocol/cgroup_id`；另用 `exec_prefix` 验证可写目录前缀执行只在目标 cgroup 内被 `lsm_bprm_check_security` 阻断，事件携带 `exec_prefix/cgroup_id`；再用 `file_access: write` 验证目标 cgroup 内读打开成功、写打开失败，事件携带 `file_access/file_flags/cgroup_id`；用 `path_prefix + file_access: write` 验证只读目录保护，目标 cgroup 内目录前缀写打开失败、读打开成功，事件携带 `path_prefix/file_access/file_flags/cgroup_id`；用 scope-only cgroup target 验证 `lsm_ptrace_traceme` 只阻断目标 cgroup 内 `PTRACE_TRACEME`，事件携带 `path=ptrace_traceme/cgroup_id`；用 `lsm_capable` 验证目标 cgroup 内 `CAP_SYS_ADMIN` 被拒绝且 scope 外允许，事件携带 `capability/cgroup_id`；再用 `lsm_task_fix_setuid`、`lsm_task_fix_setgid`、`lsm_task_fix_setgroups` 和 `lsm_cred_prepare` 验证目标 cgroup 内 credential 动作被拒绝且 scope 外允许，事件分别携带 `uid/euid/suid/setuid_flags/cgroup_id`、`gid/egid/sgid/setgid_flags/cgroup_id`、`group_count/old_group_count/cgroup_id` 与 `uid/euid/suid/gid/egid/sgid/group_count/old_group_count/cred_gfp/cgroup_id`；并用 `burst_execve` 验证可配置异常规则能够生成 `security_policy_events.anomaly-execve.jsonl`。
- `security_policy_demo` 保留为兼容回归入口，最终答辩口径应优先使用正式 `security_policy`。

完整目标态仍按下面结构扩展：

```yaml
security_policy:
  enabled: false
  mode: audit
  target_ref: web_pod

  tracing:
    syscalls:
      - execve
      - openat
      - connect
      - ptrace

  anomaly_rules:
    - name: burst_execve
      type: rate
      syscall: execve
      threshold: 20
      window_ms: 1000
      severity: medium

    - name: sensitive_path_write
      type: path
      path_prefix: /proc/sys
      access: write
      severity: high

  lsm_rules:
    - name: deny_core_pattern_write
      hook: file_open
      path: /proc/sys/kernel/core_pattern
      permissions: write
      action: deny

    - name: deny_unknown_exec
      hook: bprm_check_security
      path_prefix: /tmp/eulerpilot-deny/
      action: deny
```

最低能力线：

- syscall tracing 固定覆盖 `execve/openat/connect/ptrace` 四类。（已完成四类 audit 观测）
- 异常检测在用户态完成，BPF 侧只做过滤和事件上报。
- LSM 至少覆盖文件访问和程序执行两类强制控制。（已完成 `file_open`、`bprm_check_security`、`socket_connect`、`ptrace_traceme`、`capable`、`task_fix_setuid`、`task_fix_setgid`、`task_fix_setgroups` 与 `cred_prepare` 多目标 `target_map`，支持 `file_access=any/read/write`、文件 `path_prefix` 只读目录保护、精确 `exec_path`、前缀 `exec_prefix`、IPv4 endpoint、scoped ptrace、scoped capability、scoped setuid/setgid/setgroups credential 转换和 scoped cred_prepare credential preparation，blocked 事件已支持规则级 `rule_id/target_ref`、显式 cgroup scope、PID target 自动解析、container_id target cgroup 解析、runtime container name 解析和 k8s pod name 解析；runtime anomaly 已覆盖 `credential_churn` 生命周期聚合）
- `enforce` 默认只允许 lab 目标，不允许直接作用于系统级 cgroup。

## 8. ResourceControlSkill YAML

Resource Control 不能只保留 `cpu.weight`。当前 `resource_control` 已完成 CPU + Memory + IO 自动闭环，并已接入 `target_ref` cgroup 闭环，默认配置位于 `configs/skills.yaml`：

```yaml
- name: resource_control
  kind: runtime
  enabled: true
  config:
    mode: enforce
    controllers:
      cpu:
        max:
          enabled: true
      memory:
        enabled: true
        high:
          enabled: true
        low:
          enabled: true
        max:
          enabled: true
        reclaim:
          enabled: false
      io:
        enabled: true
        device: auto
        weight:
          enabled: true
        max:
          enabled: true
    targets: {}
    profiles:
      latency:
        cpu_max: max
        memory_low: '67108864'
        memory_high: max
        memory_max: max
        io_weight: 'default 100'
        io_max: ''
      batch:
        cpu_max: max
        memory_low: '0'
        memory_high: max
        memory_max: max
        io_weight: 'default 100'
        io_max: ''
      background:
        normal:
          cpu_max: max
          memory_high: max
          io_weight: 'default 100'
          io_max: ''
        pressure:
          cpu_max: '20000 100000'
          memory_high: '134217728'
          memory_low: '0'
          memory_max: max
          memory_reclaim: ''
          io_weight: 'default 50'
          io_max: 'auto rbps=max wbps=1048576'
```

已落地行为：

- `GateState::Active/Cooldown` 或非 `normal_profile` 时进入 pressure 模式。
- `latency` 组默认只做 `memory.low` 保护，不设置 CPU quota。
- `background` 组在 pressure 模式下写 `cpu.max`、`memory.high`、`io.weight` 与 `io.max`。
- `memory.reclaim` 已有配置开关，默认关闭，避免 one-shot 动作在每个周期重复触发。
- `controllers.io.device=auto` 会解析根文件系统所在块设备；121/122 当前均解析为 `253:0`。
- `targets + profiles.<name>.target_ref` 已接入 `TargetResolver`，支持 `type: cgroup/pid/container_id/container/k8s_pod` 解析为 cgroup path；未命中 target scope 的 workload 会跳过，不写控制器。
- 121/122 已通过 `tests/integration/test_resource_control.sh` 和 `tests/integration/test_resource_control_io.sh`，最新结果目录为 `results/resource_control/integration-20260624-160317`、`results/resource_control/integration-20260624-160349`、`results/resource_control/io-20260624-160008` 与 `results/resource_control/io-20260624-160208`。
- 121/122 已通过 `tests/integration/test_resource_control_target.sh`，结果目录为 `results/resource_control/target-20260624-172139` 与 `results/resource_control/target-20260624-172916`，验证 target cgroup 被限额、非目标 cgroup 不被误改、审计和 Agent JSONL 携带 `target_ref`。
- 真实 runtime / Pod target 演示入口已完成：`tests/integration/test_resource_control_real_runtime_target.sh` 面向 docker/podman 容器，`tests/integration/test_resource_control_real_pod_target.sh` 面向 Kubernetes lab Pod；当前 121/122 因缺 docker/podman/kubectl 输出 blocked 证据，不自动安装软件、拉镜像或创建集群资源。

target_ref 示例：

```yaml
targets:
  background_scope:
    type: cgroup
    path: /sys/fs/cgroup/eulerpilot/target-background
  build_container:
    type: container
    container_name: build-worker
    runtime: auto
  web_pod:
    type: k8s_pod
    namespace: eulerpilot-lab
    pod_name: web-demo
    container_name: nginx
profiles:
  background:
    target_ref: background_scope
```

写入 cgroup 控制器时必须：

```text
读取旧值
  -> 解析可选 target_ref
  -> 跳过 target scope 外 workload
  -> 校验新策略
  -> 写入控制器
  -> 验证是否生效
  -> 记录 ActionJournal
  -> 失败回滚
```

当前补充证据：

- CPU quota 效果指标已在 121/122 完成：`tests/integration/test_resource_control_cpu_quota.sh` 使用 `cpu.stat usage_usec`、`nr_throttled`、`throttled_usec` 对照，证明 `cpu.max=10000 100000` 后实际 CPU 使用率下降并触发 throttling。
- Redis + background CPU quota Compare Benchmark 已在 121/122 完成：`tests/benchmark/test_resource_control_redis_quota_compare.sh` 输出 `default_noisy`、`eulerpilot_no_quota`、`eulerpilot_quota` 三阶段 Redis GET/SET RPS 与 background cgroup `cpu.stat` 对照，当前作为同样 Agent 放置下的后台限额效果、throttling 和业务指标边界证据，不作为 Redis 性能提升结论。
- Redis quota profile sweep 已在 121/122 完成：`tests/benchmark/test_resource_control_redis_quota_sweep.sh` 扫描多档 background `cpu.max`，当前跨机保守默认演示 profile 暂定 `10000 100000`；`5000 100000` 只作为 121 单机候选，不直接写入默认 YAML。
- Nginx quota profile sweep 已在 121/122 完成：`tests/benchmark/test_resource_control_nginx_quota_sweep.sh` 扫描相同 background `cpu.max` profile，121/122 均推荐 `5000 100000` 作为 Nginx 场景激进候选；由于 Redis 跨机结果更保守，默认 YAML 暂不直接切到该值。
- Mixed Redis+Nginx quota profile sweep 已在 121/122 完成：`tests/benchmark/test_resource_control_mixed_quota_sweep.sh` 在同一窗口并发 Redis GET/SET 与 Nginx wrk，121 推荐 `20000 100000`，122 推荐 `50000 100000`；默认 YAML 后续若要内置混合 profile，应优先使用更保守的跨机值或在现场环境重新跑 sweep 后冻结。
- Mixed Redis+Nginx multi-resource profile benchmark 已在 121/122 完成：`tests/benchmark/test_resource_control_mixed_multi_resource.sh` 验证 `cpu.max + cpuset.cpus + memory.low/high` 组合 profile，两端均推荐 `50000 100000`，latency `memory.low=67108864`，background `memory.high=134217728`。

后续扩展：

- 在真实 container runtime / Kubernetes Pod 上复用同一 YAML 做现场演示验证。
- 继续把 multi-resource profile 接入真实 container runtime / Kubernetes lab Pod，并补跨 Skill 联动触发策略。

## 9. CPU Scheduling YAML

CPU 调度需要覆盖 Latency-first、Throughput-first 和 Mixed/Adaptive。

```yaml
cpu_scheduling:
  enabled: true
  backend: auto        # cgroup_v2 | sched_ext | auto
  mode: adaptive

  policies:
    latency_first:
      enabled: true
      target_ref: redis_cgroup

    throughput_first:
      enabled: true
      target_ref: build_cgroup

    mixed_adaptive:
      enabled: true
      latency_target_ref: redis_cgroup
      batch_target_ref: build_cgroup

  switch:
    pressure_threshold: 2.0
    cooldown_ms: 10000
```

## 10. Policy Engine 联动 YAML

当前已落地第一版 `policy_engine` Skill，默认关闭，只负责把明确的 Security anomaly 事件转换成白名单 cgroup 控制动作：

```yaml
- name: policy_engine
  kind: runtime
  enabled: false
  config:
    mode: enforce
    source:
      audit_path: reports/events/security_policy.jsonl
    watch:
      skill: security_policy
      operation: anomaly
      rule_id: burst_execve
      result: observed
    targets:
      anomaly_background:
        type: cgroup
        path: /sys/fs/cgroup/eulerpilot/policy-engine-background
    actions:
      - name: throttle_anomaly_background_cpu
        target_ref: anomaly_background
        file: cpu.max
        value: '10000 100000'
      - name: cap_anomaly_background_memory
        target_ref: anomaly_background
        file: memory.high
        value: '1048576'
```

当前验证口径：

- 触发源：`reports/events/security_policy.jsonl` 中的 `operation=anomaly`、`rule_id=burst_execve`、`result=observed`。
- 执行动作：只允许写 `/sys/fs/cgroup/` 下显式 cgroup target 的白名单控制文件。
- 审计：写入 `reports/events/policy_engine.jsonl`，包含 `cross_skill_response result=applied/restored`。
- 回滚：`ActionJournal` 记录旧值和新值，Agent stop 后恢复 `cpu.max` 与 `memory.high`。
- 121/122 验收：`results/policy_engine/security-resource-20260629-163949` 与 `results/policy_engine/security-resource-20260629-164135`。

长期目标至少支持两个联动场景的配置表达。下面是后续扩展方向，不代表当前全量完成：

```yaml
policy_engine:
  enabled: true

  rules:
    - name: protect_redis_latency
      when:
        all:
          - metric: redis.p99_ms
            op: ">"
            value: 5
          - metric: psi.cpu.some
            op: ">"
            value: 2.0
          - metric: psi.memory.some
            op: ">"
            value: 1.0
      actions:
        - skill: cpu_scheduling
          action: switch_policy
          policy: latency_first
        - skill: resource_control
          action: boost
          target_ref: redis_cgroup
        - skill: network_policy
          action: set_qos
          target_ref: web_pod
          rate: 20mbit

    - name: isolate_abnormal_container
      when:
        any:
          - event: security.anomaly.burst_execve
          - event: security.lsm.sensitive_path
      actions:
        - skill: network_policy
          action: restrict_external
        - skill: resource_control
          action: throttle
        - skill: cpu_scheduling
          action: move_to_background
```

## 11. CLI 与状态

必须支持：

```bash
./build/eulerpilot-agent --validate-config configs/agent.yaml
./build/eulerpilot-agent --list-skills
./build/eulerpilot-agent --doctor-skills --config configs/agent.yaml
./build/eulerpilot-agent --status --json
./build/eulerpilot-agent --rollback --journal run/eulerpilot/action_journal.json
```

`--status --json` 至少包含：

- Skill 名称
- enabled/running/available
- 当前 mode
- target 解析结果
- 已挂载 BPF link 数
- pinned map 数
- 最近事件
- 最近动作
- rollback 状态

## 12. 实施顺序

1. 固定 `schema_version: 2` 与 YAML 解析结构。
2. 实现 TargetResolver 最小版本。
3. 实现 CapabilityDetector 最小版本。
4. 实现 AuditBus JSONL 写入。
5. 实现 ActionJournal 写入与恢复。
6. 将 `network_policy_demo` 包装或迁移为 `network_policy`。
7. 将 `security_policy_demo` 包装或迁移为 `security_policy`。（已完成正式注册名、最小 audit/enforce 语义、YAML 驱动多目标 target_map、file/exec/socket/ptrace/capable ringbuf hit 事件、规则级 LSM blocked 事件、显式 cgroup scope、PID target 自动解析、container_id target cgroup 解析、runtime container name 解析、k8s pod name 解析、`lsm_socket_connect` scoped IPv4 endpoint enforce、`lsm_bprm_check_security` scoped exec_prefix enforce、`lsm_file_open` scoped file_access write enforce、`lsm_file_open` scoped path_prefix read-only directory enforce、`lsm_ptrace_traceme` scoped enforce、`lsm_capable` scoped capability enforce、`burst_execve` 用户态异常规则和四类 syscall tracepoint 观测）
8. 扩展 `resource_control` 的 CPU/Memory/IO 配置模型。（已完成 CPU+Memory+IO 事务化写入、审计、journal、rollback 和 121/122 集成测试）
9. 增加 Policy Engine 联动配置。（已完成 `security_policy burst_execve anomaly -> policy_engine -> cgroup cpu.max/memory.high`、`burst_connect -> resource_control + network_qos` 和真实 Pod 版联动；`burst_connect/burst_openat_sensitive/capability_abuse` 服务联动 anomaly 与 `credential_churn` 生命周期 anomaly 已在 121/122 通过）
10. 将最终质量门禁从 demo target 切换到正式 Skill target。

一句话结论：

> v2.1 的 YAML 不是单纯列出哪些 Skill 启用，而是定义目标、能力、规则、审计、动作和回滚的统一控制面；这会直接决定后续 Network、Security、Resource 能不能从 demo 走向争奖级正式 Agent。
