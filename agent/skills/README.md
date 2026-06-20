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

正式化扩展：

- `network_policy/`：Network Policy Agent，当前已形成 `cgroup/connect4`、`network_qos` TC egress 和 `network_xdp` isolated-veth XDP 三个最小闭环。
- `security_policy/`：Security Agent，后续覆盖 syscall tracing、runtime anomaly 和 BPF LSM enforce。
- `policy_advisor/`：预留的可选解释/调参建议接口，不进入当前核心资源控制热路径。
