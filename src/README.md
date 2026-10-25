# 实现状态

截至 2026-10-25（交付级构建 gen-3），`src/` 各目录均已实现并在 openEuler
24.03-LTS-SP3 上验证；逐条判定见 `docs/ACCEPTANCE_20261025.md`，线路契约与偏差见
`docs/p1/ABI_FREEZE.md`。

- `src/executor/`：唯一特权写者——flock 单实例、lease/fence、generation、
  append-only WAL、事务状态机、幂等、崩溃 reconcile、`shutdown()` 补偿退出；
  监控线程负责在位性（连续 3 次失败才回退）、TTL、漂移复查、自动通道与事件守卫；
- `src/skills/sched_skill.cpp`：生产路径 `--skill sched`（loader 挂载后经
  SCM_RIGHTS 交 link FD）与对照路径 `--skill sched-ref`（子进程托管）；cfg 每刷新
  周期再断言，class 条目按轮换抽样纠正，撤销以 `class-get` 读回确认；边界见
  `docs/p1/SCHED_SKILL_REPORT.md`；
- `src/skills/cgroup_skill.cpp`：cgroup v2 `cpu.weight` 的快照、写入（`O_TRUNC`）、
  读回、漂移纠正、marker 崩溃恢复与叠加保护；
- `src/core/`：策略校验（SchedulingPolicy / CgroupPolicy / EventArm）、目标绑定
  （pidfd、`O_PATH|O_NOFOLLOW` + kernfs inode）、FeatureWindow、规则路由、SLO 门控、
  AutoPilot 三层抑制、事件守卫判定、领域模型与工具（SHA-256、规范 JSON、RFC3339）；
- `src/evtmon/`：只读事件泵，排空 BPF 聚合 map 并经 `event.observe` 上报；
- `src/cli/`：`ep` 客户端（status/features/route/slo/observe/reconcile/validate/
  lease/apply/rollback）。

设计上未启用的能力在 apply 阶段直接拒绝并返回 `UNSUPPORTED`：`adaptive` 模式、心跳
降级路径、PMU 分类。`mock` Skill 仅用于测试，不接触内核。
