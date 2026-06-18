# rollback Skill

职责：在策略失败、Agent 异常或性能回退时恢复 cgroup 参数并退出主动调度策略。

当前阶段：

- 负责清理 `cgroup v2` 下的 `eulerpilot` 层级。
- 将已迁移任务尽可能移回根 cgroup。
- 已补第一版 `sched_ext` 回滚：
  - 停止活动中的 `scx_*` 进程
  - 清理 pinned `class_map`
  - 让系统回到 `sched_ext disabled` 状态
