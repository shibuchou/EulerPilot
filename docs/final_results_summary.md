# EulerPilot 最终结果摘要

更新时间：`2026-07-20`

本文只保留最终提交口径。早期 RUNS=1/3、SP3/OLK 候选结果作为阶段记录保留在对应结果目录和历史文档中；答辩正文以 `docs/final_evidence_index.md`、`docs/final_report_submission.md` 和 `reports/final_evidence_compact.md` 为准。

## 1. 当前主结果

| 类别 | 结果目录 | 口径 |
|------|----------|------|
| SP4 Redis sched_ext compare | `results/final/redis-scx-compare-20260724-tested-2541464-runs10` | RUNS=10，tested_code_commit=2541464 的 frozen-code 正式实验 |
| SP4 Nginx sched_ext compare | `results/final/nginx-scx-compare-20260724-tested-2541464-runs10` | RUNS=10，第二业务线边界复核 |
| Redis pressure gradient | `results/final/redis-pressure-gradient-20260724-tested-2541464-runs3` | RUNS=3，workers=0/1/2/4/8，展示干扰强度变化下的收益与代价 |
| Redis static vs Agent dynamic | `results/final/redis-static-vs-agent-20260724-tested-2541464-runs10` | RUNS=10，证明 Agent 动态调控具备自动观测、审计和 rollback；不写成永远超过人工最优 |
| Throughput-first | `results/final/throughput-first-20260724-tested-2541464-runs10` | RUNS=10，批处理优先模式争奖增强证据 |
| Mixed-Adaptive | `results/final/mixed-adaptive-20260724-tested-2541464-runs10-lite` | RUNS=10，同一 Agent 进程完成 NORMAL/ARMED/ACTIVE/COOLDOWN/NORMAL 闭环证据 |
| Agent overhead | `results/final/agent-overhead-20260724-tested-2541464-runs10` | RUNS=10，控制面开销证据 |

## 2. Redis 观察

- Redis / latency-sensitive 混布场景收益更明确。
- `cgroup v2` 和部分 `sched_ext/scx` 模式在干扰场景下能保护前台业务或抑制后台干扰。
- `noisy_scx_psi` 额外运行 PSI probe，报告中只作为带观测负载的自适应路径分析，不与无额外 probe 的模式做无条件性能优劣断言。
- `static-vs-Agent` 旧无效结果已撤下；当前最终目录为 frozen-code RUNS=10 结果，验证目标 worker 归属和 throttling 证据。

## 3. Nginx 观察

- Nginx 复核证明 EulerPilot 可以迁移到第二业务线，但收益边界更依赖 workload。
- `cgroup_v2` 在 Nginx 场景下更稳；部分 `sched_ext` 模式存在尾延迟代价。
- 最终报告不写成“所有 workload 稳定优于默认调度器”，而是强调可观测、可决策、可执行、可回滚和可解释。

## 4. 质量门禁

最新 SP4 质量门禁：

- 记录：`reports/final_quality_gate_20260720-stage3-performance.log`
- 结果：`22/22 P0` 通过
- 可选项：`100` 轮 Agent smoke 通过，`5` 轮 doctor 通过

最终 evidence：

```text
entries=40
missing_required=0
warnings=0
```

## 5. 可提交边界

已支撑：

- 统一 Agent / Skills / Policy Engine 架构
- `cgroup v2` 稳定后端与 `sched_ext/scx` 增强后端
- Resource / Network / Security 三方向 OS Agent 扩展
- Kubernetes 旁路隔离验证
- Web Console 可视化演示
- 41 条 final evidence compact

仍需人工补充：

- 正式 8-10 分钟演示视频文件或公开链接。
