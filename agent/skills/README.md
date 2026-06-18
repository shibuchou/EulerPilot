# Agent Skills

EulerPilot Skill 是 Agent 调用具体系统能力的标准插件边界。

统一生命周期：

```text
init -> observe -> analyze -> decide -> act -> rollback -> export_metrics
```

第一阶段必须落地：

- `cpu_schedule/`：sched_ext/scx 调度能力。
- `cgroup_control/`：cgroup v2 兜底执行能力。
- `benchmark/`：一键实验和报告生成。
- `rollback/`：策略失败或异常时恢复系统状态。

演示级扩展：

- `network_policy/`：TC/XDP 或 socket filter 演示。
- `security_policy/`：LSM/tracepoint 安全事件观测演示。
- `policy_advisor/`：预留的可选解释/调参建议接口，不进入当前核心资源控制热路径。
