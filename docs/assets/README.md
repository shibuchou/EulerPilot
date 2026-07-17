# docs/assets

作用：存放项目架构图、闭环流程图和可编辑图源。

## 推荐使用

- `eulerpilot_architecture_board.svg`：对外展示优先使用的分层项目架构总览图，适合 README、答辩页和报告首页。
- `eulerpilot_closed_loop_flow.svg`：Agent 从观测、决策、执行到 rollback 和 evidence 输出的闭环流程图。
- `eulerpilot_architecture_detailed.drawio`：可在 draw.io Desktop 或 VS Code Draw.io 插件中继续编辑的架构图源文件。
- `eulerpilot_architecture_detailed.spec.yaml`：draw.io 图的结构化源文件，便于后续自动生成或批量调整。
- `eulerpilot_architecture_detailed.mmd`：Mermaid 版详细架构图源文件。
- `eulerpilot_closed_loop_flow.mmd`：Mermaid 版闭环流程图源文件。

## 口径要求

- `SP3`：稳定主路径是 `cgroup v2 + eBPF/PSI + Policy Engine + Skills`。
- `OLK-6.6`：用于提前验证 `sched_ext/scx`、`ScxExecutor`、`class_map` 和 `scx_eulerpilot`。
- `SP4`：发行环境已完成适配验证；`sched_ext/scx` 路径基于 SP4 官方源码自编译启用 `CONFIG_SCHED_CLASS_EXT` 的内核完成复核，不声称发行默认内核直接支持 `sched_ext`。

