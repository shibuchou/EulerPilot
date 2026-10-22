# P1 控制面骨架报告

日期：2026-10-21；状态：**控制面执行服务骨架完成并在 SP3 通过测试；真实 sched_ext Skill 未接入。**
证据：[sp3-build-test.txt](../../evidence/p1/sp3-build-test.txt)、
[sp3-env-after-perf.json](../../evidence/p1/sp3-env-after-perf.json)、
[ABI_FREEZE.md](ABI_FREEZE.md)。

> 2026-10-21 后续更新：参考 `SchedSkill`（`sched-ref`）已接入，并在 SP3 上通过真实
> attach/detach 与崩溃孤儿回收测试；`SchedulingPolicy` schema/语义校验与 cgroup TargetBinding
> 也已完成。本报告关于控制面、WAL、lease、幂等、reconcile 的结论仍然有效；
> 数据面与策略边界分别以 [SCHED_SKILL_REPORT.md](SCHED_SKILL_REPORT.md)、
> [POLICY_BINDING_REPORT.md](POLICY_BINDING_REPORT.md) 为准。

## 1. 结论摘要

按上一轮确定的顺序，先立控制面，不继续加数据面功能。已实现：

- `ep-executord`：唯一写者、flock 主机锁、lease/fence、generation、append-only WAL、
  事务状态机、崩溃 reconcile、幂等、Unix socket 控制通道。
- `ep`：`status / lease / apply / rollback / reconcile` CLI。
- Skill 接口与测试用 `MockSkill`；生产 `sched` 当时注册为 **UNSUPPORTED**（后续已由 `sched-ref` 替换，
  见 [SCHED_SKILL_REPORT.md](SCHED_SKILL_REPORT.md)），骨架阶段不会假装执行。
- 单元测试与端到端集成测试；WSL（g++ 13）与 SP3（g++ 12.3）均 2/2 通过。

**不包含**：BPF 加载、cgroup/TC 写入、真实调度策略、策略 schema 语义校验、target binding、
Agent 规则分类、任何性能结论。

## 2. 交付物

| 组件 | 文件 | 作用 |
|---|---|---|
| 领域模型 | `include/ep/domain.hpp`, `src/core/domain.cpp` | 状态/错误码、ActionResult |
| 工具 | `include/ep/util.hpp`, `src/core/util.cpp` | SHA-256、规范 JSON、RFC3339、随机 token |
| WAL | `include/ep/journal.hpp`, `src/executor/journal.cpp` | fsync 追加、撕裂尾行截断、幂等结果、未完成事务 |
| 所有权 | `include/ep/ownership.hpp`, `src/executor/ownership.cpp` | flock、instance、generation、lease/fence |
| 事务引擎 | `include/ep/executor.hpp`, `src/executor/executor.cpp` | apply/rollback/reconcile/status/lease |
| Skill 接口 | `include/ep/skill.hpp` | probe/plan/snapshot/apply/verify/rollback/reconcile |
| 测试 Skill | `include/ep/mock_skill.hpp`, `src/skills/mock_skill.cpp` | 可补偿的单值状态 |
| 未实现占位 | `include/ep/unsupported_skill.hpp` | 明确返回 UNSUPPORTED |
| 服务 | `src/executor/main.cpp` | Unix socket、SO_PEERCRED、信号、pid/socket 清理 |
| CLI | `src/cli/main.cpp` | 客户端与 envelope 构造 |
| 构建 | `CMakeLists.txt`, `third_party/nlohmann/` | C++20，唯一第三方依赖 MIT JSON |
| 测试 | `tests/unit/test_main.cpp`, `tests/integration.sh` | 单元 + 端到端 |

## 3. 构建与测试

| 环境 | 编译器 | 结果 |
|---|---|---|
| WSL Ubuntu 24.04 | g++ 13.3.0 / CMake 3.28.3 | 构建 0 warning；ctest 2/2 |
| openEuler 24.03-LTS-SP3 | g++ 12.3.1 / CMake 3.27.9 | 构建 0 warning；ctest 2/2，重跑仍通过 |

产物哈希（SP3）：`ep` `f34b43cf…`、`ep-executord` `6f2a0750…`。
源码包 sha256 `fac701f9…`（不含 `.git`，195 个文件）。

测试覆盖的核心不变量：

- 同 idempotency key + 同内容 → 返回原结果，不重复执行、不推进 generation；
- 同 key + 不同内容 → `IDEMPOTENCY_CONFLICT`；
- stale fence / 错误 owner / 过期 lease → `OWNERSHIP_CONFLICT`；
- `expected_generation` 不匹配 → `GENERATION_CONFLICT`，不自动覆盖；
- apply 失败 → 补偿成功为 `ROLLED_BACK`，补偿失败为 `RECOVERY_REQUIRED` 并 degraded；
- 外部改动后的 rollback → 拒绝覆盖，`RECOVERY_REQUIRED`；
- 启动时对 PREPARED/APPLYING/RECOVERY_REQUIRED 事务 reconcile；
- 第二实例因 flock 启动失败；
- 生产模式 `sched` 返回 `UNSUPPORTED`，socket 在 SIGTERM 后清理。

## 4. 骨架阶段的生产模式行为（实测，后由 sched-ref 取代）

```text
ep status  -> status OK, skill=sched, generation=0
ep apply --file design/examples/redis-policy.json
           -> REJECTED
              error.code=UNSUPPORTED
              error.detail="sched skill is not implemented in the P1 skeleton"
```

这是刻意的 fail-closed：骨架阶段宁可拒绝，也不返回假的 VERIFIED。

## 5. 设计取舍与契约偏差

1. **线路包装**：action-envelope schema 是 `additionalProperties: false` 且不含策略正文，
   因此请求为 `{"operation", "envelope", "policy"}`（测试模式用 `"intent"`）。
   envelope 本体逐字段符合 `design/schemas/action-envelope.schema.json`。
2. **结果新增字段**：`transaction_id/action_id/snapshot_id`。原契约的结果示例没有它们，
   但 `rollback_of` 必须能寻址，否则 CLI 无法发起回滚。
3. **新增错误码**：`APPLY_FAILED`、`RECOVERY_REQUIRED`、`IDEMPOTENCY_CONFLICT`。
   契约写明错误码"包括"上述集合，视为允许扩展；已记录在 ABI 文档。
4. **幂等摘要不含 generation/lease/fence**：集成测试发现，若包含 `expected_generation`，
   客户端刷新状态后的正常重试会被误判为冲突。已固定为"意图摘要"。
5. **journal 顺序**：COMMITTED 记录先于 `state.json` 持久化；启动时以 journal 的最大 generation
   为准。若 state 写失败，进程返回 DEGRADED，重启后从 journal 恢复。
6. **单线程服务**：每连接一个请求；P2 若需要并发，先加队列和背压，不能简单多线程写 WAL。

## 6. 环境缺口已补

SP3 安装 `cmake 3.27.9`、`perf 6.6.0-145.3.35.166`。重跑 SchedPilot `env_check`：
`ok=32 warn=3 fail=0`，PMU 为 OK（cycles/instructions 可读）。P0 报告中的
"perf 缺失/PMU 假阴性"限制在环境层面已解除；P0 那次 A/B/C smoke 本身仍没有 perf 计数，
不能追溯补记。

## 7. 未实现与不证明

- 骨架阶段真实 `sched_ext` Skill 未接入，`policy.apply` 生产路径只到 UNSUPPORTED；
  后续已由 `sched-ref` 接入，见 [SCHED_SKILL_REPORT.md](SCHED_SKILL_REPORT.md)。
- 骨架阶段没有按 schema 校验 `SchedulingPolicy`；后续已补字段/范围/语义校验，见
  [POLICY_BINDING_REPORT.md](POLICY_BINDING_REPORT.md)。
- 骨架阶段 `target_bindings` 恒为空；后续已有 cgroup 路径 + inode/device/boot_id/cgroup_id
  绑定，pid 目标/pidfd 仍未实现。
- apply 在入口、skill.apply 后、skill.verify 后检查 deadline；超时走补偿并返回
  DEADLINE_EXCEEDED。rollback 不因 deadline 中断。
- 没有并发客户端、背压、日志轮转、审计导出。
- 没有性能数据；本报告不包含任何 v2 性能结论。

## 8. 下一步（P1 剩余）

1. 实现 `SchedSkill` 的 probe/snapshot/apply/verify/rollback，先只支持"无副作用 probe + 拒绝"，
   再逐步接入参考 loader 的 pin/ownership；每一步都在 SP3 上做 attach/detach 集成测试。
2. 加 `SchedulingPolicy` schema 与语义校验（范围、驻留/TTL、BG 显式角色、重复目标）。
3. 实现 pid 目标的 TargetBinding（pidfd/starttime），并接入 class map；
   cgroup 侧已完成 inode/device/boot_id/cgroup_id。
4. 把 journal 接入 CLI 的审计查询，补日志轮转与磁盘满降级路径。
5. 之后才进入 P2 的 FeatureWindow、规则分类与 SLO 门控。
