# ResourceControlSkill 说明

本文说明 EulerPilot Resource Control Agent 的当前能力、配置方式、事务化执行流程和验收入口。当前阶段已从早期 `cpu.weight + cgroup.procs` 扩展为 CPU + Memory + IO 自动闭环。

## 能力定位

`resource_control` 是 EulerPilot 的 cgroup v2 执行 Skill。它接收 workload 分类结果与 PSI gate 状态，在用户态做策略决策，并把动作落到 `/sys/fs/cgroup/eulerpilot/{latency,batch,background}` 三个实验 cgroup。

当前已落地能力：

- CPU：`cpu.weight`、`cpu.max`、`cpuset.cpus`、`cpuset.mems`
- Memory：`memory.high`、`memory.low`、`memory.max`
- IO：`io.weight`、`io.max`
- Target：`target_ref` 可解析 cgroup、PID、container ID、runtime container name 和 Kubernetes Pod cgroup
- 可选 Memory reclaim：`memory.reclaim` 已有配置开关，默认关闭，后续只在明确 pressure 策略中启用
- 自动模式：`GateState::Active/Cooldown` 或非 `normal_profile` 时进入 pressure 模式
- 事务化执行：读取旧值、校验新值、写入控制器、复读验证、写 `AuditBus`、写 `ActionJournal`、停止时恢复旧值

当前安全边界：

- 未配置 `target_ref` 时，只操作 `/sys/fs/cgroup/eulerpilot/latency`、`batch`、`background` 三个实验 cgroup，并把命中 workload 迁入对应 profile cgroup。
- 配置 `target_ref` 时，先通过 `TargetResolver` 解析真实 cgroup，再只对命中该 cgroup 的 workload 写控制器；不命中的进程会被标记为 `target-scope-mismatch`，不会被误限流。
- 默认不会修改系统 root cgroup；显式 `target_ref` 只应指向实验 cgroup、容器 cgroup 或比赛验证环境中明确允许管理的 Pod cgroup。
- `latency` 组默认不做 CPU quota 强限，只通过 `memory.low` 做保护。
- `background` 组在 pressure 模式下使用 `cpu.max`、`memory.high` 和 `io.max` 做限额。
- `memory.max` 默认保持 `max`，避免误杀实验进程。
- `io.max` 默认解析根文件系统所在块设备；121/122 当前均为 `253:0`。

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

说明：

- `mode: enforce` 表示允许写 cgroup 控制器；如果 Agent 以 dry-run 启动，代码仍不会写入。
- `targets: {}` 是默认空 target 集合；需要按容器或 Pod 管控时，在这里声明 target，再在 `profiles.<name>.target_ref` 中引用。
- 测试脚本会把 pressure 策略收紧为 `cpu.max=10000 100000`、`memory.high=1048576`，便于在短时间内验证闭环。
- `controllers.memory.reclaim.enabled` 默认关闭，避免每个控制周期重复触发 one-shot reclaim。
- `controllers.io.device=auto` 会解析 `/` 所在块设备；IO 集成测试固定使用 `253:0`。

### Target 示例

直接管理已存在 cgroup：

```yaml
targets:
  background_scope:
    type: cgroup
    path: /sys/fs/cgroup/eulerpilot/target-background
profiles:
  background:
    target_ref: background_scope
```

后续接入真实容器或 Pod 时，配置形式保持一致：

```yaml
targets:
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
    target_ref: build_container
```

当前实现对 `type: container_id/container/k8s_pod/pid/cgroup` 统一解析为 cgroup path，再复用同一套 CPU/Memory/IO 事务化写入与 rollback。

## 执行流程

每次 cgroup 控制器写入遵循同一流程：

```text
select profile
  -> build CPU/Memory/IO desired values
  -> resolve optional target_ref
  -> skip samples outside target scope
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

- CPU+Memory 回归 121：`results/resource_control/integration-20260624-160317/summary.txt`
- CPU+Memory 回归 122：`results/resource_control/integration-20260624-160349/summary.txt`
- IO controller 121：`results/resource_control/io-20260624-160008/summary.txt`
- IO controller 122：`results/resource_control/io-20260624-160208/summary.txt`
- target_ref 改动后 IO 回归 121：`results/resource_control/io-regression-20260624-174400/summary.txt`
- target_ref cgroup 闭环 121：`results/resource_control/target-20260624-172139/summary.txt`
- target_ref cgroup 闭环 122：`results/resource_control/target-20260624-172916/summary.txt`
- runtime target 闭环 121：`results/resource_control/runtime-target-20260624-212403/summary.txt`
- runtime target 闭环 122：`results/resource_control/runtime-target-20260624-212529/summary.txt`

测试命令：

```bash
cd /root/EulerPilot
tests/integration/test_resource_control.sh
tests/integration/test_resource_control_io.sh
tests/integration/test_resource_control_target.sh
tests/integration/test_resource_control_runtime_target.sh
```

测试覆盖：

- 构建 `eulerpilot-agent`
- 初始化 cgroup v2 CPU/Memory/IO controller
- 启动 `yes` 作为 background workload
- 以 `always-active + --active` 启动 Agent
- 验证 background 组写入 `cpu.max=10000 100000`
- 验证 background 组写入 `memory.high=1048576`
- 触发内存压力并验证 `memory.events high` 计数增加
- 验证 Agent 停止后 `cpu.max` 和 `memory.high` 恢复旧值
- 验证 `resource_control_events.jsonl` 包含 `cpu.max`、`memory.high` 的 `applied` 与 rollback 事件
- IO 测试验证 background 组写入 `io.max=253:0 rbps=max wbps=1048576` 与 `io.weight=default 50`
- IO 测试使用 direct write 验证 `io.stat wbytes` 增长和限速耗时增加
- IO 测试验证 Agent 停止后 `io.max` 和 `io.weight` 恢复旧值
- IO 测试验证 `resource_control_events.jsonl` 包含 `io.max`、`io.weight` 的 `applied` 与 `restored` 事件
- Target 测试验证 `profiles.background.target_ref` 能解析到指定 cgroup，只对目标 cgroup 写 `cpu.max/memory.high`，非目标 cgroup 保持原值
- Target 测试验证 Agent JSONL 和 `resource_control_events.jsonl` 都携带 `target_ref` 与目标 cgroup path，并在退出后恢复旧值
- Runtime target 测试验证 `container_id`、runtime container name 和 `k8s_pod` 名称解析均能落到目标 cgroup，只对目标 cgroup 写 `cpu.max/memory.high`，scope 外 cgroup 保持原值
- Runtime target 测试使用 fake `crictl/kubectl` 固定解析路径，不依赖真实容器服务；它验证的是 Resource Control 与 `TargetResolver` 的解析、写入、审计和回滚链路，真实容器/Kubernetes lab Pod 仍作为后续现场演示项

当前 121 结果摘要：

```text
result=pass
cpu_max_pressure=10000 100000
memory_high_pressure=1048576
memory_high_events_before=0
memory_high_events_after=590
old_cpu_max=max 100000
old_memory_high=max
```

当前 122 结果摘要：

```text
result=pass
cpu_max_pressure=10000 100000
memory_high_pressure=1048576
memory_high_events_before=0
memory_high_events_after=993
old_cpu_max=max 100000
old_memory_high=max
```

当前 121 IO 结果摘要：

```text
result=pass
io_device=253:0
io_max_pressure=253:0 rbps=max wbps=1048576
io_weight_pressure=default 50
baseline_time_s=0.026
limited_time_s=6.684
baseline_wbytes_after=4194304
limited_wbytes_after=8388608
old_io_weight=default 100
```

当前 122 IO 结果摘要：

```text
result=pass
io_device=253:0
io_max_pressure=253:0 rbps=max wbps=1048576
io_weight_pressure=default 50
baseline_time_s=0.025
limited_time_s=13.784
baseline_wbytes_after=4194304
limited_wbytes_after=8388608
old_io_weight=default 100
```

当前 121 runtime target 结果摘要：

```text
result=pass
target_types=container_id,container,k8s_pod
container_id_cpu_max_pressure=10000 100000
container_name_cpu_max_pressure=10000 100000
k8s_pod_cpu_max_pressure=10000 100000
container_id_memory_high_pressure=1048576
container_name_memory_high_pressure=1048576
k8s_pod_memory_high_pressure=1048576
```

当前 122 runtime target 结果摘要：

```text
result=pass
target_types=container_id,container,k8s_pod
container_id_cpu_max_pressure=10000 100000
container_name_cpu_max_pressure=10000 100000
k8s_pod_cpu_max_pressure=10000 100000
container_id_memory_high_pressure=1048576
container_name_memory_high_pressure=1048576
k8s_pod_memory_high_pressure=1048576
```

## 后续 TODO

- 在真实 docker/podman/crictl 或 Kubernetes lab Pod 环境中补现场演示，把 fake runtime 自测升级为真实运行时证据。
- 增加更稳定的 CPU quota 效果指标，例如 `cpu.stat usage_usec` 与 throttled 计数对照。
