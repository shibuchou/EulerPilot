# Policy Engine Skill 设计与验收说明

更新时间：`2026-06-30`

本文说明 EulerPilot `policy_engine` Skill 的当前能力、配置方式、安全边界和验收入口。它的定位不是替代 `security_policy`、`resource_control` 或 `network_policy`，而是在统一 Agent 内消费各 Skill 事件，并把异常信号转换为可审计、可回滚的处置动作。

## 当前完成度

当前版本已完成三条跨 Skill 联动链路，其中第三条已经把 v3.1 的 lab cgroup/netdev 目标推进到真实 Kubernetes Pod cgroup 与 Pod host veth。

第一条链路：

```text
security_policy burst_execve anomaly
  -> policy_engine
  -> resource_control cgroup cpu.max / memory.high
  -> ActionJournal
  -> Agent stop rollback
```

第二条 v3.1 链路：

```text
security_policy burst_connect anomaly
  -> policy_engine decision
  -> resource_control demo_cgroup cpu.max / memory.high
  -> network_qos lab_netdev tc/tbf 2mbit
  -> AuditBus + ActionJournal
  -> Agent stop rollback
```

第三条 v3.2 真实 Pod 链路：

```text
security_policy burst_connect anomaly
  -> policy_engine decision
  -> target_ref=lab_pod(type=k8s_pod)
  -> resource_control 解析为真实 Pod cgroup 并写 cpu.max / memory.high
  -> network_qos 解析为真实 Pod host veth 并写 tc/tbf 2mbit
  -> AuditBus + ActionJournal
  -> Agent stop rollback
```

121 已通过 `tests/integration/test_policy_engine_security_network_resource.sh --repeat 10`，122 已通过同一核心集成测试；真实 Pod 联动也已在 121/122 通过。最新结果目录为：

```text
v3.1 lab 121: results/policy_engine/security-network-resource-20260629-214952
v3.1 lab 122: results/policy_engine/security-network-resource-20260629-215950
real Pod 121: results/policy_engine/real-pod-security-network-resource-20260630-k3s-121-v1
real Pod 122: results/policy_engine/real-pod-security-network-resource-20260630-k3s-122-v1
```

## 独立配置

v3.1 强联动不改默认安全配置，使用专用配置：

```text
configs/policy_engine_security_network_resource.yaml
configs/policy_engine_security_network_resource.skills.yaml
```

运行入口：

```bash
./build/eulerpilot-agent --validate-config configs/policy_engine_security_network_resource.yaml
sudo tests/integration/test_policy_engine_security_network_resource.sh
```

默认动作作用域：

```text
resource_control:
  target_ref = demo_cgroup
  cpu.max = 20000 100000
  memory.high = 134217728

network_qos:
  target_ref = lab_netdev
  tc/tbf rate = 2mbit
```

`resource_control` target 是 cgroup，`network_qos` target 是测试脚本创建的 lab netdev，二者在配置和测试中明确区分。

## transaction_id 证据链

v3.1 所有跨 Skill 事件都带统一事务字段：

```json
{
  "transaction_id": "pe-v3-1-xxxx",
  "trigger_event_id": "sec-xxxx",
  "policy_id": "security_network_resource_response",
  "stage": "decision|applied|restored|failed"
}
```

验收时必须能用同一个 `transaction_id` 串起：

```text
security anomaly
policy_engine decision
resource_control applied/restored
network_qos applied/restored
ActionJournal action records
```

## 安全边界

`policy_engine` 采用白名单动作，不执行外部命令：

- 默认 `enabled: false`，只在明确配置和测试中启用。
- 只监听本机 JSONL 事件。
- cgroup target 必须位于 `/sys/fs/cgroup/` 下。
- cgroup 控制文件白名单：`cpu.max`、`cpu.weight`、`memory.high`、`memory.low`、`memory.max`、`io.max`、`io.weight`。
- 写入 `memory.high=134217728` 前检查 `memory.max == max` 或 `memory.max > 134217728`，否则跳过 memory 子动作并记录 reason。
- netdev target 默认只允许 `ep-*`、`eulerpilot-*`、`lab-*` 前缀，默认拒绝 `eth*`、`ens*`、`eno*`、`wlan*`、`bond*`、`br*`、`cni*`、`flannel*`；当 target 为 `type: k8s_pod/pod` 且通过 `eulerpilot-lab` resolver 解析出 runtime host veth 时，允许非生产 veth 名并继续拒绝生产/CNI 主设备前缀。
- 写入前读取旧值，写入后复读验证，并在 stop/rollback 恢复旧值或删除 lab qdisc。
- 多动作事务失败时按逆序回滚已成功动作，例如 Resource 已写入但 Network QoS 失败时必须恢复 Resource 旧值。

## 事件与审计

事件文件：

- `reports/events/security_policy.jsonl`
- `reports/events/policy_engine.jsonl`
- `reports/events/resource_control.jsonl`
- `reports/events/network_policy.jsonl`
- `run/eulerpilot/action_journal.jsonl`

核心验收字段：

- `policy_id=security_network_resource_response`
- `operation=cross_skill_response`
- `stage=decision|applied|restored|failed`
- `transaction_id`
- `trigger_event_id`
- `target_ref`
- `file` 或 `rate`
- `old_value`
- `new_value`
- `result=applied|restored|failed|skipped`

## 验收入口

第一条联动：

```bash
sudo tests/integration/test_policy_engine_security_resource.sh
```

第二条联动：

```bash
sudo tests/integration/test_policy_engine_security_network_resource.sh
sudo tests/integration/test_policy_engine_security_network_resource.sh --repeat 10
```

真实 Pod 联动：

```bash
sudo EULERPILOT_KUBECONFIG=/etc/rancher/k3s/k3s.yaml \
  tests/integration/test_policy_engine_real_pod_network_resource.sh
```

第二条测试会完成：

1. 构建 Agent、Security demo 和 Network QoS demo。
2. 创建 `/sys/fs/cgroup/eulerpilot/policy-engine-v3-resource`。
3. 创建 isolated veth `ep-veth-pe0 <-> ep-veth-pe1` 和 netns `ep-pe-ns`。
4. 启动 Agent，并使用 `burst_connect` 触发 security anomaly。
5. 验证 `cpu.max=20000 100000` 和 `memory.high=134217728` 写入。
6. 验证 `ep-veth-pe0` 上出现 `tc/tbf rate 2mbit`。
7. 验证限速前后吞吐证据、qdisc 证据、事务事件和 ActionJournal。
8. 停止 Agent 后验证 cgroup 和 qdisc rollback。
9. 模拟 Network QoS 失败，要求 Policy Engine 回滚已写入 Resource Control 动作。
10. `--repeat 10` 连续 apply/rollback 无残留。

## 后续扩展

v3.2 已完成真实 Kubernetes Pod 联动：同一个 `target_ref=lab_pod(type=k8s_pod)` 可在 `policy_engine` 内按动作类型解析为 Pod cgroup 或 Pod host veth，并保留 `target_type=k8s_pod`、`resolved_target_type=cgroup|netdev`、Pod namespace/name/UID、`transaction_id` 和 ActionJournal 证据。后续争奖增强重点转向 XDP on Pod host veth、更多 Security anomaly 与最终答辩证据压缩。