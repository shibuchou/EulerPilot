# 实现状态

P1 控制面骨架已实现：

- `src/executor/`：唯一写者（flock）、lease/fence、generation、append-only WAL、事务状态机、崩溃 reconcile；
- `src/skills/mock_skill.cpp`：测试用可补偿 Skill，不接触内核；
- `src/skills/sched_skill.cpp`：`sched-ref` 参考适配器，子进程托管 v2 自建 `ep-sched-loader`
  （`sched/`，GPL-2.0），真实 attach/detach、pin 所有权与孤儿回收；边界见
  `docs/p1/SCHED_SKILL_REPORT.md`；
- `src/cli/`：`ep` 客户端（status/lease/apply/rollback/reconcile）；
- `src/core/`：领域模型、SHA-256、规范 JSON、RFC3339。

真实 sched_ext Skill、cgroup 写入和 Agent 决策尚未实现；生产模式对 `sched` 返回 UNSUPPORTED。
契约与偏差见 `docs/p1/ABI_FREEZE.md`，实现与测试结果见 `docs/p1/P1_SKELETON_REPORT.md`。
