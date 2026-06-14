# cpu_schedule Skill

职责：对接 sched_ext/scx，实现 workload class 到调度队列和 profile 的执行。

当前阶段已经进入最小原型：

- 已在独立 `OLK-6.6` 环境上验证 `sched_ext` 基础能力
- 已能通过用户态 Agent 的 `--backend sched_ext` 启停 `scx`
- 已形成“证据触发 -> scx 启动 -> class_map 写入 -> 退出回退”的第一版执行闭环

当前限制：

- 当前 `scx_eulerpilot` 仍是第一版原型
- 还没有完整的类内公平性与更细颗粒度反馈
- 还未进入正式性能实验主线

下一步目标：

- 稳定 `class_map`
- 让 latency / batch / background 与 scx 内部队列建立稳定对应关系
- 将 `ScxExecutor` 从“全局启停”推进到“可解释的类级调度执行”
