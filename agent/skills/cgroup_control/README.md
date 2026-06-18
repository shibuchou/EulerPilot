# cgroup_control Skill

职责：使用 cgroup v2 cpu.weight、cpu.max、cpuset 等能力提供资源管控兜底路径。

当前状态：

- `PSI` 与 unified `cgroup v2` 已在官方 openEuler 24.03 LTS SP3 内核上验证可用。
- `cgroup_control` 已升级为第一阶段主执行路径，而不只是兜底路径。
- Agent 已能基于第一版 workload 分类结果执行 profile 级 `cpu.weight` 调整和 `cgroup.procs` 分配。
- `cpuset` 隔离能力已预留到执行设计中，但当前主实验先以 `cpu.weight + cgroup.procs` 为稳定路径。

第一阶段：

- 实现 `mixed_profile` 下后台 workload 限制和 rollback。
- 维护 `/sys/fs/cgroup/eulerpilot/{latency,batch,background}` 层级。
- 通过 `scripts/setup_cgroup_v2.sh` 和 `scripts/rollback.sh` 提供初始化与恢复。
