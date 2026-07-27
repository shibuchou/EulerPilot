# cgroup_control / resource_control Skill

职责：使用 cgroup v2 为 EulerPilot 提供 CPU + Memory + IO 资源管控执行路径。

## 当前状态

- `resource_control` 是正式 Skill 名称；`cgroup_control` 作为目录名保留历史语义。
- 已支持 `/sys/fs/cgroup/eulerpilot/{latency,batch,background}` 三组实验 cgroup。
- 封版稳定路径支持 `cpu.weight`、`cpu.max`。
- `cpuset.cpus`、`cpuset.mems` 保留为显式实验开关，默认关闭，不进入当前 release 的完整能力和性能主结论。
- 已支持 `memory.high`、`memory.low`、`memory.max`。
- 已支持 `io.weight`、`io.max`。
- `memory.reclaim` 预留为 one-shot 动作，默认关闭。
- 写控制器前会读取旧值并校验，写后复读验证，动作进入 `AuditBus` 和 `ActionJournal`。
- Agent `stop/rollback` 会恢复本进程生命周期内修改过的旧值。

## 安全边界

- 仅操作 EulerPilot 实验 cgroup，不写任意系统 cgroup。
- `latency` 组默认使用 `memory.low` 保护，不默认设置 CPU quota。
- `background` 组在 pressure 模式下使用 `cpu.max` 与 `memory.high` 限制干扰。
- `background` 组在 pressure 模式下使用 `io.max` 限制写带宽，并用 `io.weight` 降低相对权重。
- `memory.max` 默认保持 `max`，避免测试进程被误杀。

## 关键文件

- `agent/src/executors.cpp`：cgroup v2 执行与事务化写入。
- `agent/src/builtin_skills.cpp`：`resource_control` YAML 配置解析与 gate pressure 模式接入。
- `configs/skills.yaml`：默认 CPU/Memory/IO 策略。
- `scripts/setup_cgroup_v2.sh`：初始化 CPU/memory/io controller 与实验 cgroup；仅在 `EULERPILOT_ENABLE_CPUSET=1` 时启用实验性 cpuset 分组。
- `scripts/rollback.sh`：恢复 CPU/Memory/IO 控制器默认值并清理实验 cgroup。
- `tests/integration/test_resource_control.sh`：CPU+Memory 自动闭环集成测试。
- `tests/integration/test_resource_control_io.sh`：IO controller 集成测试。
- `docs/resource_control_skill.md`：设计、配置和验收说明。

## 验收

最新通过结果：

- CPU+Memory 回归 121：`results/resource_control/integration-20260624-160317/summary.txt`
- CPU+Memory 回归 122：`results/resource_control/integration-20260624-160349/summary.txt`
- IO controller 121：`results/resource_control/io-20260624-160008/summary.txt`
- IO controller 122：`results/resource_control/io-20260624-160208/summary.txt`

测试验证 `background` 组 pressure 策略写入 `cpu.max=10000 100000`、`memory.high=1048576`、`io.max wbps=1048576`，触发 `memory.events high` 与 `io.stat wbytes` 计数增长，并在 Agent 退出后恢复旧值。121 和 122 均已通过。
