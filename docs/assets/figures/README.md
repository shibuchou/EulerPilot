# 文档图示与可编辑源文件

统一采用蓝色表示控制处理、绿色表示执行结果、琥珀色表示约束、红色表示拒绝恢复。实线表示主要流程，虚线表示观测、反馈或持久化关系；具体语义以各图文字为准。

这些图描述当前实现。自动通道默认关闭；sched/cgroup 是每实例二选一的效果域。软件预算与 aging 不等于硬实时保证；性能数据构建与最终验收构建分别记录。

| 图示 | SVG | 可编辑源文件 |
|---|---|---|
| EulerPilot v2 总体架构 | [查看](system-architecture.svg) | [Draw.io](system-architecture.drawio) |
| 策略生效与补偿流程 | [查看](policy-apply-flow.svg) | [Draw.io](policy-apply-flow.drawio) |
| 事务日志状态与恢复 | [查看](transaction-state-machine.svg) | [Draw.io](transaction-state-machine.drawio) |
| sched_ext 数据面与公平调度 | [查看](scheduler-data-path.svg) | [Draw.io](scheduler-data-path.drawio) |
| eBPF 事件驱动资源闭环 | [查看](event-driven-control-loop.svg) | [Draw.io](event-driven-control-loop.drawio) |
| 事件守卫的判定与动作 | [查看](event-guard-decision.svg) | [Draw.io](event-guard-decision.drawio) |
| 启动恢复与降级判定 | [查看](startup-recovery.svg) | [Draw.io](startup-recovery.drawio) |
| SP3 部署与构建路径 | [查看](deployment-paths.svg) | [Draw.io](deployment-paths.drawio) |
| 分层验证与严格验收 | [查看](verification-layers.svg) | [Draw.io](verification-layers.drawio) |
| cgroup 效应的完整生命周期 | [查看](cgroup-effect-lifecycle.svg) | [Draw.io](cgroup-effect-lifecycle.drawio) |
| 现场演示八步时间线 | [查看](demo-timeline.svg) | [Draw.io](demo-timeline.drawio) |
| 构建与实验证据溯源 | [查看](evidence-provenance.svg) | [Draw.io](evidence-provenance.drawio) |
| 同预算的配对实验设计 | [查看](experiment-design.svg) | [Draw.io](experiment-design.drawio) |
| 自动切换的受限决策通道 | [查看](bounded-autopilot.svg) | [Draw.io](bounded-autopilot.drawio) |

## 生成与验证

布局、文案与连线的唯一维护入口为 [`scripts/build_diagrams.py`](../../../scripts/build_diagrams.py)。修改后重建全部 SVG、Draw.io 与布局清单：

```bash
python scripts/build_diagrams.py
# 安装有 Playwright 与 Chrome 的 Node 环境；浏览器路径可用 DIAGRAM_CHROME 指定
node scripts/render_diagrams.cjs
python scripts/check_diagrams.py
```

`render_diagrams.cjs` 生成 `build/diagram-qa/png/`，并检查文字边界、画布裁切和连线穿框。Draw.io 源文件可直接导入 diagrams.net；SVG 使用标准矢量形状与文本，无外部资源或脚本。图内中文依赖本机中文字体，推荐 Microsoft YaHei 或 Noto Sans CJK SC。

Word 导出保留原始 SVG，并附带 PNG 兼容预览；PDF 由 Word 原生导出。重建正式交付件前先生成至临时目录并审阅，避免覆盖未核验产物。

```bash
python scripts/build_docx.py --all --output-dir build/diagram-qa/exports --pdf
python scripts/check_diagrams.py --exports build/diagram-qa/exports
```

本次只更新图示与文档导出，不产生新的内核验收或性能实验结果。
