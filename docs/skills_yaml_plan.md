# EulerPilot Skills 与 YAML 能力规划 v2.1

更新时间：`2026-06-21`

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
- `network_qos` 会选择 `rules.*.hook=tc_egress` 的第一条规则，并解析其 `target_ref` 指向的 `type: netdev` target。
- `network_xdp` 会读取最多 8 条 `rules.*.hook=xdp` 规则，并要求这些规则解析到同一个 `type: netdev` target。
- 审计事件已输出 `rule_id` 与 `target_ref`。
- k8s_pod 到 veth 解析、TC 多规则、XDP UDP 规则扩展尚未完成。

## 7. SecurityPolicySkill YAML

Security 使用统一目标引用和 observe/audit/enforce 模式。

当前已落地的最小 schema 使用 `targets + rules + target_ref` 描述固定 demo path target，默认 `audit` 且默认 disabled：

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
    rules:
      - name: deny_demo_secret_open
        hook: lsm_file_open
        target_ref: demo_secret
        action: deny
```

当前语义：

- `audit` 模式 attach BPF LSM + `execve/openat/connect/ptrace` tracepoint，但通过 `policy_map.enforce=0` 允许目标文件访问和 demo 执行脚本运行，并通过 ringbuf 输出 `result=observed` 命中事件。
- `audit` 事件当前覆盖 `event_hook=lsm_file_open`、`event_hook=lsm_bprm_check_security`、`event_hook=sys_enter_execve`、`event_hook=sys_enter_openat`、`event_hook=sys_enter_connect`、`event_hook=sys_enter_ptrace`。
- `enforce` 模式复用 `bpf/security_policy_demo.bpf.c`，在 `lsm/file_open` 上拒绝固定 demo secret 文件，在 `lsm/bprm_check_security` 上拒绝固定 demo 执行脚本，并通过 ringbuf 输出 `result=blocked` 命中事件；四类 syscall 当前只做观测，不阻断。
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
- LSM 至少覆盖文件访问和程序执行两类强制控制。（已完成 `file_open` 与 `bprm_check_security` 固定 demo target）
- `enforce` 默认只允许 lab 目标，不允许直接作用于系统级 cgroup。

## 8. ResourceControlSkill YAML

Resource Control 不能只保留 `cpu.weight`。

```yaml
resource_control:
  enabled: true
  mode: audit
  target_ref: web_pod

  controllers:
    cpu:
      enabled: true
      weight:
        min: 100
        max: 10000
      max:
        enabled: true

    memory:
      enabled: true
      high:
        enabled: true
      low:
        enabled: true
      max:
        enabled: false
      reclaim:
        enabled: auto

    io:
      enabled: true
      weight:
        enabled: true
      max:
        enabled: true

  policy:
    pressure_window_ms: 5000
    cooldown_ms: 15000
    max_actions_per_minute: 4
```

写入 cgroup 控制器时必须：

```text
读取旧值
  -> 校验新策略
  -> 写入控制器
  -> 验证是否生效
  -> 记录 ActionJournal
  -> 失败回滚
```

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

至少支持两个联动场景的配置表达。

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
7. 将 `security_policy_demo` 包装或迁移为 `security_policy`。（已完成正式注册名、最小 audit/enforce 语义、固定 path/file exec ringbuf hit 事件和四类 syscall tracepoint 观测）
8. 扩展 `resource_control` 的 CPU/Memory/IO 配置模型。
9. 增加 Policy Engine 联动配置。
10. 将最终质量门禁从 demo target 切换到正式 Skill target。

一句话结论：

> v2.1 的 YAML 不是单纯列出哪些 Skill 启用，而是定义目标、能力、规则、审计、动作和回滚的统一控制面；这会直接决定后续 Network、Security、Resource 能不能从 demo 走向争奖级正式 Agent。
