# EulerPilot 最终结果摘要

更新时间：`2026-07-27`

本文是最终交付收口阶段的结果口径页。旧 `20260724-tested-2541464` 性能目录保留为 historical/provisional 过程证据，不再作为 final positive evidence；正式收益、有效性和边界结论以 `tested_code_commit=7a99d87048f4f2040377354bfe0ce21401664642` 与 formal artifact `ef1baebec7ac138acd0eb1a59fc3880ca550330ef89f87615e8579a0ef264240` 绑定后的 RUNS=10 套件为准。

## 1. 当前主结果

| 类别 | 正式结果目录 | 口径 |
|------|--------------|------|
| SP4 Redis sched_ext compare | `/root/eulerpilot-runs/7a99d87048f4f2040377354bfe0ce21401664642/formal-experiments/20260726-162050/redis-scx-compare-runs10` | formal artifact RUNS=10；修正 default noisy baseline 后生成 |
| SP4 Nginx sched_ext compare | `/root/eulerpilot-runs/7a99d87048f4f2040377354bfe0ce21401664642/formal-experiments/20260726-162050/nginx-scx-compare-runs10` | formal artifact RUNS=10；作为第二业务线收益和边界证据 |
| Throughput-first | `/root/eulerpilot-runs/7a99d87048f4f2040377354bfe0ce21401664642/formal-experiments/20260726-162050/throughput-first-runs10` | validity=pass；使用本轮 counter delta 和 class-aware dispatch accounting |
| Mixed-Adaptive | `/root/eulerpilot-runs/7a99d87048f4f2040377354bfe0ce21401664642/formal-experiments/20260726-162050/mixed-adaptive-runs10` | validity=pass；同一 Agent instance 连续验证 NORMAL -> ARMED -> ACTIVE -> COOLDOWN -> NORMAL |
| Agent overhead | `/root/eulerpilot-runs/7a99d87048f4f2040377354bfe0ce21401664642/formal-experiments/20260726-162050/agent-overhead-runs10` | validity=pass；用户态 Agent 开销约 0.45%-0.55% 单核 |

## 2. 性能结论

- `cgroup v2` 是官方发行内核上的稳定资源治理主路径。修正 baseline 后，Redis GET/INCR 在受干扰场景中出现明确正向收益；Nginx 在本次 RUNS=10 中也呈现 RPS 和 P99 改善。
- Redis GET：`noisy_cgroup_v2` 相对 `noisy_default` 的 RPS 平均提升 68.30%，95% CI 为 `[50.74%, 85.86%]`；P99 改善均值 27.23%，但 95% CI 跨零，尾延迟结论按边界表述。
- Redis INCR：`noisy_cgroup_v2` RPS 平均提升 83.09%，95% CI 为 `[65.21%, 100.98%]`；P99 改善 52.27%，95% CI 为 `[26.41%, 78.13%]`。
- Nginx：`noisy_cgroup_v2` RPS 平均提升 7.07%，95% CI 为 `[4.51%, 9.63%]`；P99 改善 18.49%，95% CI 为 `[13.87%, 23.11%]`。
- `sched_ext/scx` 已证明控制链、batch dispatch 记账和 mixed-adaptive 状态闭环可运行；性能表现按 workload 拆分，不能宣传为所有场景稳定提升。部分 SCX PSI/always-active 组合存在明显回退，作为调优边界如实保留。

## 3. 功能有效性结论

- Throughput-first：10 轮均通过 `collection_valid`、`classification_valid`、`dispatch_accounting_valid`、`workload_completion_valid` 和 `scheduler_stability_valid`，旧版 `enqueue_batch>0` 但无 batch dispatch 证据的问题已修复。
- Mixed-Adaptive：10 轮均由同一 Agent instance 写入连续 trace，phase marker 与 gate event 使用统一 event sequence，闭环状态链满足 `NORMAL -> ARMED -> ACTIVE -> COOLDOWN -> NORMAL`。
- Agent overhead：observe-only cgroup、active cgroup、active sched_ext 三种模式平均 CPU 占用约 0.45%-0.55% 单核，RSS 平均约 7.4-7.5 MB。

## 4. 质量门禁

- SP4 final gate：`reports/final_quality_gate_20260726-v6-sp4-final.log`，29/29 P0、100 轮 Agent smoke、5 轮 doctor-safe 通过。
- SP3 compatibility final gate：`reports/final_quality_gate_20260726-v6-sp3-final.log`，10/10 通过，并保留 capability matrix。
- Evidence release gate：`python3 scripts/collect_final_evidence.py --validate-release` 已通过，当前口径为：

```text
entries=42
missing_required=0
warnings=0
```

## 5. 可提交边界

已支撑：

- 统一 Agent / Skills / Policy Engine 架构。
- `cgroup v2` 稳定后端与 `sched_ext/scx` 增强后端。
- Resource / Network / Security 三方向 OS Agent 扩展。
- Kubernetes 旁路隔离验证。
- Web Console 可视化演示。
- 42 条 evidence compact，其中旧性能结果由 `evidence/evidence_status_overrides.json` 标记为 historical/provisional/invalid，不进入 final positive evidence。

仍需人工补充：

- PPT 和项目说明书最终版由用户确认后加入答辩提交材料目录。
- 是否创建 final tag / release 需要用户单独确认。
