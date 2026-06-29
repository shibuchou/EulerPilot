# policy_engine Skill

职责：把不同 Skill 的事件转成受控动作，形成 EulerPilot 的跨 Agent 联动层。

## 当前状态

- 已注册正式 `policy_engine` Skill，默认 disabled。
- 当前第一版监听 `reports/events/security_policy.jsonl` 中的 `security_policy` anomaly 事件。
- 默认匹配 `operation=anomaly`、`rule_id=burst_execve`、`result=observed`。
- 匹配后按 YAML `actions` 对显式 cgroup target 写入白名单 cgroup v2 控制器。
- 写入前保存旧值，`stop/rollback` 恢复旧值。
- 动作写入 `reports/events/policy_engine.jsonl` 和 `run/eulerpilot/action_journal.jsonl`。

## 安全边界

- 默认 disabled，只有显式启用才执行联动动作。
- 第一版只允许 `type: cgroup` target，且路径必须位于 `/sys/fs/cgroup/` 下。
- 只允许写 `cpu.max`、`cpu.weight`、`memory.high`、`memory.low`、`memory.max`、`io.max`、`io.weight`。
- 不读取或执行外部命令，不自动创建 target cgroup。
- 当前只做一次性响应，避免同一异常事件反复扩大限制。

## 关键文件

- `agent/src/builtin_skills.cpp`：`policy_engine` 的事件监听、动作写入和 rollback 实现。
- `configs/skills.yaml`：默认 disabled 配置模板。
- `tests/integration/test_policy_engine_security_resource.sh`：Security anomaly 触发 Resource Control 降级的集成测试。

## 验收

最小验收链路：

```text
security_policy burst_execve anomaly
  -> policy_engine watch
  -> target cgroup cpu.max/memory.high 降级
  -> policy_engine.jsonl 记录 cross_skill_response
  -> ActionJournal 记录旧值/新值
  -> Agent stop 后恢复旧值
```
