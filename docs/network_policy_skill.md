# NetworkPolicySkill 设计与实施说明

更新时间：`2026-06-19`

对应阶段：`docs/next_phase_plan_v2_1.md` 阶段 B

## 1. 目标

将当前 `network_policy_demo` 从单端口 `cgroup/connect4` 演示升级为正式 `network_policy` Skill。

正式完成线：

- `cgroup/connect4`：目标 cgroup 作用域、audit/enforce、命中计数、rollback。
- TC QoS：eBPF classifier 识别目标流量，HTB/TBF 执行限速。
- XDP：只挂 isolated veth 或 lab Pod veth，实现 drop/pass 和命中统计。
- 所有事件进入 `AuditBus`。
- 所有挂载、map pin、qdisc、XDP attach 进入 `ActionJournal`。
- 所有目标解析通过 `TargetResolver`。

## 2. 命名迁移

| 当前名称 | 阶段 B 目标 |
|----------|-------------|
| `network_policy_demo` | `network_policy` |
| `bpf/network_policy_demo.bpf.c` | 第一阶段可保留，作为 connect4 子能力实现 |
| `scripts/cleanup_network_policy_demo.sh` | 迁移为正式 rollback/cleanup 入口 |
| `bench/psi/run_network_policy_smoke.sh` | 迁移或补充为 `tests/integration/test_network_policy.sh` |

迁移原则：

- 代码可以渐进迁移，文档和 CLI 最终口径必须使用 `network_policy`。
- demo 名称只能作为兼容或回归测试出现，不能作为最终交付主口径。

## 3. YAML 模型

阶段 B 使用 `targets + rules + target_ref`，禁止所有 hook 共用一个全局 cgroup target。

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

## 4. Hook 作用域

| Hook | Target 要求 | 默认安全限制 |
|------|-------------|--------------|
| `cgroup_connect4` | `type: cgroup` | 只作用于 demo/lab cgroup |
| `tc_ingress` / `tc_egress` | `type: netdev` 或 `type: k8s_pod` 解析出的 veth | 默认只允许 lab veth |
| `xdp` | `type: netdev` | 默认只允许 isolated veth，不允许管理网卡 |

XDP 不按进程或 cgroup 挂载。它只能按网卡/接口挂载，并通过 IP、协议、端口等包字段匹配规则。

## 5. 三阶段实现顺序

### B1. connect4 正式化

目标：

- 保留现有 cgroup/connect4 BPF 程序作为第一阶段核心。
- 从 YAML 读取 target 和 rules。
- 使用 `TargetResolver` 解析 cgroup path。
- 支持 `audit` 和 `enforce`：
  - `audit`：命中规则但不拒绝。
  - `enforce`：目标 cgroup 内 connect 被拒绝。
- 输出 `AuditBus` 事件。
- attach/link 信息写入 `ActionJournal`。
- stop/rollback 后无 link、map、cgroup 残留。

验收：

- 目标 cgroup connect 指定端口被拒绝。
- 非目标 cgroup 100% 正常。
- audit 模式不改变行为但有事件。
- rollback 后目标操作恢复。

### B2. TC QoS

技术路线：

```text
eBPF TC classifier
  -> 匹配 Pod/veth/五元组
  -> 设置 classid 或 mark
  -> HTB/TBF 负责稳定整形
  -> Agent 动态更新速率
```

验收：

- 目标速率与配置误差不超过 +/-10%。
- 非目标流量吞吐不低于基线 90%。
- rollback 后 qdisc/class/filter 清理干净。

### B3. isolated-veth XDP

目标：

- 创建专用 netns/veth 或使用 lab Pod veth。
- XDP attach 只允许目标 veth。
- 支持 drop/pass、命中计数、动态规则更新。

当前最小实现：

- 新增 `network_xdp` 独立注册名，默认 disabled。
- 新增 `bpf/network_xdp_demo.bpf.c`，使用 XDP generic mode 在 isolated veth 上匹配协议和端口。
- 当前集成测试固定验证 ICMP drop；后续扩展 TCP/UDP、多规则和 Pod veth。
- `AuditBus` rollback 事件记录 `pass_count/drop_count/byte_count`。
- `ActionJournal` 记录 ifname、XDP mode、rule id 和 target_ref。

验收：

- isolated netns 到 host veth 的 ICMP 在 enforce 模式下被 drop。
- 卸载后流量恢复。
- audit 模式不挂 XDP。
- 不影响 SSH 和主机管理网卡。

## 6. AuditBus 事件字段

Network 事件最低字段：

```json
{
  "skill": "network_policy",
  "policy_id": "network_policy",
  "rule_id": "deny_redis_port",
  "mode": "audit",
  "target": {
    "cgroup_id": "12345",
    "namespace": "eulerpilot-lab",
    "pod": "web-demo"
  },
  "operation": "connect4",
  "evidence": {
    "protocol": "tcp",
    "dst_port": "6379",
    "hook": "cgroup_connect4"
  },
  "action": "deny",
  "result": "matched",
  "severity": "info"
}
```

## 7. ActionJournal 记录

Network 动作最低记录：

- cgroup attach link id 或 pinned path。
- BPF map pinned path。
- tc qdisc/class/filter 原状态。
- XDP ifname/ifindex 和 attach mode。
- rule id 和 target_ref。
- rollback 是否完成。

## 8. 目录和交付物

计划新增或更新：

```text
docs/network_policy_skill.md
agent/skills/network_policy/README.md
tests/integration/test_network_policy.sh
scripts/demo_network_policy_product.sh
reports/events/network_policy.jsonl
results/network_policy/
```

## 9. 当前状态

状态：`阶段 B 进行中，connect4 子能力已完成，TC QoS 最小闭环已完成，isolated-veth XDP 最小闭环已完成，YAML v2 target_ref 已落地`

已具备：

- `TargetResolver / AuditBus / ActionJournal / CapabilityDetector` 最小代码。
- 现有 `network_policy_demo` cgroup/connect4 demo。
- 现有质量门禁可保证 demo 默认 disabled 且无 BPF/LSM 残留。
- 正式 `network_policy` Skill 名称已注册，`network_policy_demo` 作为兼容名称保留。
- `network_policy` 默认 disabled，默认 `audit` 模式。
- `configs/skills.yaml` 已升级为 `schema_version: 2`，默认配置使用 `targets + rules + target_ref`。
- `audit` 模式不挂载 BPF，只写 AuditBus/ActionJournal 事件，避免误阻断流量。
- `enforce` 模式使用 BPF `policy_map` 动态配置目标端口，不再依赖 BPF 常量端口。
- `stats_map` 已记录 allow/deny 命中计数，rollback 审计事件包含最终计数。
- `tests/integration/test_network_policy.sh` 已基于 YAML v2 验证 audit 模式不挂 cgroup BPF，并写入 `reports/events/network_policy.jsonl`。
- `tests/integration/test_network_policy.sh` 已基于 YAML v2 验证 enforce 模式下目标 cgroup 访问动态端口 `18081` 被拒绝，非目标 cgroup 访问正常，退出后无 pinned link 或 cgroup attachment 残留。
- connect4 审计事件已包含 `rule_id=deny_demo_port` 与 `target_ref=demo_cgroup`。
- `network_qos` 子能力已注册，默认 disabled。
- `bpf/network_qos_tc.bpf.c` 已实现 TC egress classifier，负责按协议/端口配置命中并累计 packet/byte 统计。
- `network_qos` enforce 模式通过 libbpf `bpf_tc_attach` 挂载 classifier，并通过 TBF root qdisc 执行限速。
- `tests/integration/test_network_qos_tc.sh` 已基于 YAML v2 验证 lab netns/veth、audit 不改 qdisc、enforce 安装 clsact + TBF、流量命中统计和 rollback 无残留。
- TC QoS 审计事件已包含 `rule_id=limit_lab_egress` 与 `target_ref=lab_veth`。
- `network_xdp` 子能力已注册，默认 disabled。
- `bpf/network_xdp_demo.bpf.c` 已实现 XDP generic filter，负责按协议/端口配置执行 pass/drop 并累计 pass/drop/byte 统计。
- `network_xdp` enforce 模式通过 libbpf `bpf_xdp_attach` 以 generic mode 挂载程序，rollback 通过 `bpf_xdp_detach` 卸载。
- `tests/integration/test_network_xdp.sh` 已基于 YAML v2 验证 lab netns/veth、audit 不挂 XDP、enforce drop ICMP、rollback 后连通性恢复。
- XDP 审计事件已包含 `rule_id=drop_icmp_lab` 与 `target_ref=lab_xdp_veth`。
- 完整质量门禁已通过，日志为 `reports/final_quality_gate_20260619_xdp.log`。
- 121 最新集成测试目录：`results/network_policy/integration-20260619-142347/`。
- 122 最新集成测试目录：`results/network_policy/integration-20260619-122352/`。
- 121 最新 TC QoS 集成测试目录：`results/network_policy/qos-tc-20260619-142357/`。
- 122 最新 TC QoS 集成测试目录：`results/network_policy/qos-tc-20260619-122403/`。
- 121 最新 XDP 集成测试目录：`results/network_policy/xdp-20260619-142321/`。
- 122 最新 XDP 集成测试目录：`results/network_policy/xdp-20260620-180651/`。

下一步：

1. 为 TC QoS 增加速率误差 Benchmark 和多规则验证。
2. 将 isolated-veth XDP 扩展到 TCP/UDP 多规则和 lab Pod veth。
3. 将 `TargetResolver` 从 cgroup/connect4 扩展到 netdev/k8s_pod 解析。
