# EulerPilot 最终交付包索引

更新时间：`2026-07-26`

## 1. 项目定位

EulerPilot 是面向 openEuler 的自适应资源管控 Agent，围绕“观测 -> 决策 -> 执行 -> 反馈 -> 证据”形成系统级闭环。当前交付材料以 SP4 主验证仓库为核心，SP3/121 和 OLK-6.6/122 作为历史验证与 sched_ext 对照线。

三条验证线：

- SP3 / 121：`cgroup v2` 稳定主闭环和历史回归对照。
- OLK-6.6 / 122：`sched_ext/scx` 提前验证和对照线。
- SP4 / 123：当前核心验证和 v6 收口线，完成发行环境适配、SP4 官方源码自编译 sched_ext 内核功能复核、K8s/Web Console；旧 RUNS=10 结果已降级为 historical/provisional 或 invalid historical。

## 2. 核心代码

- `agent/`：Agent Runtime、Skill Manager、Policy Engine、Executor、Metrics。
- `bpf/`：workload observer、Network Policy、TC QoS、XDP、Security Policy。
- `sched/`：`scx_eulerpilot` 调度器和 sched_ext map/DSQ 分流。
- `configs/`：Agent、Skills、Policy、PSI Gate、final evidence manifest。
- `scripts/`：环境检查、构建、回滚、证据收集、质量门禁和报告生成。
- `tests/`：C++ 单元测试、集成测试和 benchmark。
- `web_console/`：Evidence-first 旁路展示控制台。

## 3. 最终证据

- evidence compact：`reports/final_evidence_compact.md`、`reports/final_evidence_compact.json`
- 当前口径：`entries=41`、`missing_required=0`、`warnings=8`
- 质量门禁：历史 `reports/final_quality_gate_20260720-stage3-performance.log` 保留；v6 当前通过缩短版 preflight 29/29，最终 gate 待 formal artifact 后重跑
- 图表材料：`reports/final_figures/`
- Dashboard：`reports/dashboard/index.html`

## 4. 关键结果目录

| 证据 | 路径 |
|------|------|
| SP4 Redis RUNS=10 historical/provisional | `results/final/redis-scx-compare-20260724-tested-2541464-runs10` |
| SP4 Nginx RUNS=10 historical/provisional | `results/final/nginx-scx-compare-20260724-tested-2541464-runs10` |
| Redis 压力递增梯度 historical/provisional | `results/final/redis-pressure-gradient-20260724-tested-2541464-runs3` |
| Redis 静态 vs Agent 动态 historical/provisional | `results/final/redis-static-vs-agent-20260724-tested-2541464-runs10` |
| Throughput-first invalid historical | `results/final/throughput-first-20260724-tested-2541464-runs10` |
| Mixed-Adaptive invalid historical | `results/final/mixed-adaptive-20260724-tested-2541464-runs10-lite` |
| Agent overhead historical/provisional | `results/final/agent-overhead-20260724-tested-2541464-runs10` |
| K8s / Web Console 旁路验证 | `results/k8s/sp4-validation-20260708-023552` |

## 5. 文档主链

- `README.md`：项目首页。
- `docs/final_submission_guide.md`：最终提交指南。
- `docs/final_report_submission.md`：最终报告主稿。
- `docs/final_evidence_index.md`：最终证据索引。
- `docs/architecture.md`：系统架构。
- `docs/defense_final.md`：答辩主文档。
- `docs/defense_qa.md`：答辩问答。
- `docs/demo_final_runbook.md`：现场演示流程。
- `docs/demo_video_recording_script.md`：8-10 分钟视频录制脚本。
- `submission/README.md`：提交包入口。

## 6. 代码仓库

- GitHub：`https://github.com/shibuchou/EulerPilot`
- GitLink：`https://gitlink.org.cn/HxQj0tp0pG/mxoedzsyzygka`

## 7. 当前结论

项目已经完成系统主体实现、SP4 主验证线、sched_ext/scx 自编译内核功能复核、三方向 OS Agent 扩展、Policy Engine 跨 Skill 联动、Web Console 展示和 41 条 evidence compact。当前仍需完成 v6 Candidate Gate、Formal Artifact Gate、修正 baseline 后的正式随机化实验、release gate 和正式演示视频。
