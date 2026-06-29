# Policy Engine Skill 设计与验收说明

更新时间：`2026-06-29`

本文说明 EulerPilot `policy_engine` Skill 的当前能力、配置方式、安全边界和验收入口。它的定位不是替代 `security_policy`、`resource_control` 或 `network_policy`，而是在统一 Agent 内消费各 Skill 事件，并把异常信号转换为可审计、可回滚的处置动作。

## 当前完成度

当前版本完成了第一条跨 Skill 联动链路：

```text
security_policy burst_execve anomaly
  -> policy_engine 监听 security_policy JSONL 事件
  -> 对显式 cgroup target 写入 cpu.max / memory.high
  -> policy_engine.jsonl 记录 cross_skill_response
  -> ActionJournal 记录旧值和新值
  -> Agent stop 后恢复旧值
```

121 与 122 都已通过 `tests/integration/test_policy_engine_security_resource.sh`。该测试会创建独立 cgroup `/sys/fs/cgroup/eulerpilot/policy-engine-background`，运行 background workload，触发 `security_policy` 的 `burst_execve` anomaly，然后验证 `policy_engine` 把目标 cgroup 降级为：

```text
cpu.max=10000 100000
memory.high=1048576
```

Agent 退出后，测试确认旧值恢复为：

```text
cpu.max=max 100000
memory.high=max
```

## YAML 配置

默认配置位于 `configs/skills.yaml`，`policy_engine` 默认关闭。启用时使用 schema v2 Skill 配置：

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

当前实现只接受 `type: cgroup` target，目标路径必须位于 `/sys/fs/cgroup/` 下。第一版不自动创建 cgroup，也不解析 container/Pod；真实容器和 Pod target 继续由 `resource_control` 与 `TargetResolver` 负责。

## 安全边界

`policy_engine` 第一版采用保守边界：

- 默认 `enabled: false`，只在明确配置和测试中启用。
- 只监听本机 JSONL 事件，不执行外部命令。
- 只支持 cgroup v2 控制文件写入。
- cgroup 目标路径必须以 `/sys/fs/cgroup/` 开头。
- 控制文件采用白名单：`cpu.max`、`cpu.weight`、`memory.high`、`memory.low`、`memory.max`、`io.max`、`io.weight`。
- 对同一轮 Agent 生命周期只响应一次匹配事件，避免异常风暴导致重复写入。
- 每次写入先读取旧值，写入后复读验证，并在 stop/rollback 恢复旧值。

这些边界保证 `policy_engine` 是可演示的联动层，而不是不受控的通用 shell 执行器。

## 事件与审计

触发源来自 `reports/events/security_policy.jsonl`，当前匹配条件为：

```text
skill=security_policy
operation=anomaly
rule_id=burst_execve
result=observed
```

处置事件写入 `reports/events/policy_engine.jsonl`，核心字段包括：

- `skill=policy_engine`
- `operation=cross_skill_response`
- `source_skill=security_policy`
- `source_rule=burst_execve`
- `target_ref=anomaly_background`
- `file=cpu.max|memory.high`
- `old_value`
- `new_value`
- `result=applied|restored`

事务记录写入 `run/eulerpilot/action_journal.jsonl`，用于证明旧值、新值、目标路径和回滚动作都可追踪。

## 验收入口

不依赖 Kubernetes 的验证入口：

```bash
sudo tests/integration/test_policy_engine_security_resource.sh
```

脚本会完成以下检查：

1. 构建 `agent` 和 `security-policy-demo`。
2. 初始化 cgroup v2 CPU/Memory controller。
3. 创建目标 cgroup 并启动 background workload。
4. 启用 `security_policy` audit anomaly 与 `policy_engine` enforce。
5. 连续执行系统 `true` 触发 `burst_execve`。
6. 验证目标 cgroup 出现 `cpu.max=10000 100000` 与 `memory.high=1048576`。
7. 验证 `security_policy` anomaly 事件和 `policy_engine` applied 事件。
8. 验证 `ActionJournal` 存在对应记录。
9. 停止 Agent 后验证 `cpu.max` 与 `memory.high` 恢复旧值。
10. 复制 summary、事件和 journal 到结果目录。

当前双机结果：

- 121：`results/policy_engine/security-resource-20260629-163949`
- 122：`results/policy_engine/security-resource-20260629-164135`

两个结果目录已互相同步，便于现场只看任一机器的交付材料。

## 后续扩展

下一步不应把 `policy_engine` 扩成任意动作引擎，而应沿着比赛演示路径补两类可解释能力：

- Security 更多 anomaly 规则：例如敏感路径异常、短时连接突增、异常 capability 请求，再复用当前处置链路。
- Network QoS 联动：当 `security_policy` 或 workload 指标触发异常时，调用 `network_qos` 对对应 netdev/container/Pod target 限速，并与 Resource Control 同时回滚。

所有新增动作仍必须满足：显式 target、白名单动作、复读验证、审计事件、ActionJournal 和 stop rollback。
