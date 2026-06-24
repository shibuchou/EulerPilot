# ResourceControlSkill 说明

本文说明 EulerPilot Resource Control Agent 的当前能力、配置方式、事务化执行流程和验收入口。当前阶段已从早期 `cpu.weight + cgroup.procs` 扩展为 CPU + Memory 自动闭环；IO 控制仍作为下一阶段增强项保留。

## 能力定位

`resource_control` 是 EulerPilot 的 cgroup v2 执行 Skill。它接收 workload 分类结果与 PSI gate 状态，在用户态做策略决策，并把动作落到 `/sys/fs/cgroup/eulerpilot/{latency,batch,background}` 三个实验 cgroup。

当前已落地能力：

- CPU：`cpu.weight`、`cpu.max`、`cpuset.cpus`、`cpuset.mems`
- Memory：`memory.high`、`memory.low`、`memory.max`
- 可选 Memory reclaim：`memory.reclaim` 已有配置开关，默认关闭，后续只在明确 pressure 策略中启用
- 自动模式：`GateState::Active/Cooldown` 或非 `normal_profile` 时进入 pressure 模式
- 事务化执行：读取旧值、校验新值、写入控制器、复读验证、写 `AuditBus`、写 `ActionJournal`、停止时恢复旧值

当前安全边界：

- 只操作 `/sys/fs/cgroup/eulerpilot/latency`、`batch`、`background` 三个实验 cgroup。
- 默认不会修改系统 root cgroup 或任意业务 cgroup。
- `latency` 组默认不做 CPU quota 强限，只通过 `memory.low` 做保护。
- `background` 组在 pressure 模式下使用 `cpu.max` 和 `memory.high` 做限额。
- `memory.max` 默认保持 `max`，避免误杀实验进程。

## YAML 配置

默认配置在 `configs/skills.yaml` 的 `resource_control.config` 下：

```yaml
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
profiles:
  latency:
    cpu_max: max
    memory_low: '67108864'
    memory_high: max
    memory_max: max
  batch:
    cpu_max: max
    memory_low: '0'
    memory_high: max
    memory_max: max
  background:
    normal:
      cpu_max: max
      memory_high: max
    pressure:
      cpu_max: '20000 100000'
      memory_high: '134217728'
      memory_low: '0'
      memory_max: max
      memory_reclaim: ''
```

说明：

- `mode: enforce` 表示允许写 cgroup 控制器；如果 Agent 以 dry-run 启动，代码仍不会写入。
- 测试脚本会把 pressure 策略收紧为 `cpu.max=10000 100000`、`memory.high=1048576`，便于在短时间内验证闭环。
- `controllers.memory.reclaim.enabled` 默认关闭，避免每个控制周期重复触发 one-shot reclaim。

## 执行流程

每次 cgroup 控制器写入遵循同一流程：

```text
select profile
  -> build CPU/Memory desired values
  -> read old cgroup file value
  -> validate desired value
  -> write cgroup file
  -> read back and verify
  -> append reports/events/resource_control.jsonl
  -> append run/eulerpilot/action_journal.jsonl
  -> restore old values on stop/rollback
```

`cpu.max` 的验证包含语义匹配：写入 `max` 后内核可能读回 `max 100000`，代码会把它视为同一含义。

## 验收入口

最新通过结果：

- `results/resource_control/integration-20260624-150312/summary.txt`
- `results/resource_control/integration-20260624-150312/resource_control_events.jsonl`
- `results/resource_control/integration-20260624-151241/summary.txt`
- `results/resource_control/integration-20260624-151241/resource_control_events.jsonl`

测试命令：

```bash
cd /root/EulerPilot
tests/integration/test_resource_control.sh
```

测试覆盖：

- 构建 `eulerpilot-agent`
- 初始化 cgroup v2 CPU/Memory controller
- 启动 `yes` 作为 background workload
- 以 `always-active + --active` 启动 Agent
- 验证 background 组写入 `cpu.max=10000 100000`
- 验证 background 组写入 `memory.high=1048576`
- 触发内存压力并验证 `memory.events high` 计数增加
- 验证 Agent 停止后 `cpu.max` 和 `memory.high` 恢复旧值
- 验证 `resource_control_events.jsonl` 包含 `cpu.max`、`memory.high` 的 `applied` 与 rollback 事件

当前 121 结果摘要：

```text
result=pass
cpu_max_pressure=10000 100000
memory_high_pressure=1048576
memory_high_events_before=0
memory_high_events_after=501
old_cpu_max=max 100000
old_memory_high=max
```

当前 122 结果摘要：

```text
result=pass
cpu_max_pressure=10000 100000
memory_high_pressure=1048576
memory_high_events_before=0
memory_high_events_after=992
old_cpu_max=max 100000
old_memory_high=max
```

## 后续 TODO

- 将 IO 控制扩展为独立可演示、可回滚能力，优先考虑 `io.weight` 与 `io.max`。
- 将 Resource Control 与 Network/Security 的 `TargetResolver` 统一，支持 container/Pod target 后再落 cgroup。
- 增加更稳定的 CPU quota 效果指标，例如 `cpu.stat usage_usec` 与 throttled 计数对照。
