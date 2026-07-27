# EulerPilot 最终结果摘要

更新时间：`2026-07-26`

本文是封版前结果口径页。v6 复审后，现有 SP4 RUNS=10 性能结果全部降级为 provisional/historical：旧原始结果目录保持只读，不改写哈希；`evidence/evidence_status_overrides.json` 决定这些结果是否能进入最终正向证据。正式收益数字必须等待修复 default baseline、Candidate Gate、formal `artifact_id` 和 Formal Artifact Gate 后重新随机化运行。

## 1. 当前主结果

| 类别 | 结果目录 | 口径 |
|------|----------|------|
| SP4 Redis sched_ext compare | `results/final/redis-scx-compare-20260724-tested-2541464-runs10` | provisional historical；default baseline 与 SCX artifact provenance 待 formal artifact 重跑 |
| SP4 Nginx sched_ext compare | `results/final/nginx-scx-compare-20260724-tested-2541464-runs10` | provisional historical；作为 workload 边界历史数据保留 |
| Redis pressure gradient | `results/final/redis-pressure-gradient-20260724-tested-2541464-runs3` | provisional historical；修复 baseline 后需重跑 |
| Redis static vs Agent dynamic | `results/final/redis-static-vs-agent-20260724-tested-2541464-runs10` | provisional historical；旧 default_noisy 使用受控 baseline，已修脚本但需重跑 |
| Throughput-first | `results/final/throughput-first-20260724-tested-2541464-runs10` | invalid historical；旧 SCX batch enqueue 缺 class-aware dispatch 证据 |
| Mixed-Adaptive | `results/final/mixed-adaptive-20260724-tested-2541464-runs10-lite` | invalid historical；旧三阶段未证明同一 Agent instance 和单 writer trace |
| Agent overhead | `results/final/agent-overhead-20260724-tested-2541464-runs10` | provisional historical；需绑定 Candidate Gate 和 formal artifact |

## 2. Redis 观察

- 早期实验显示 Redis / latency-sensitive 混布场景存在正向趋势，但最终收益数字以修复基线后的 formal artifact 实验为准。
- 当前只能确定 `cgroup v2` 是官方发行内核上的稳定执行后端；`sched_ext/scx` 路径需在重新构建的 formal artifact 上重新给出有效性和性能结论。
- `noisy_scx_psi` 额外运行 PSI probe，报告中只作为带观测负载的自适应路径分析，不与无额外 probe 的模式做无条件性能优劣断言。
- `static-vs-Agent` 旧结果已通过 status override 降级；修复后的脚本把 `default_noisy` 放入 `/sys/fs/cgroup/eulerpilot-bench/<run-id>` 对等 cgroup，但还没有作为正式证据重跑。

## 3. Nginx 观察

- Nginx 复核证明 EulerPilot 可以迁移到第二业务线，但收益边界更依赖 workload。
- `cgroup_v2` 在 Nginx 场景下更稳；部分 `sched_ext` 模式存在尾延迟代价。
- 最终报告不写成“所有 workload 稳定优于默认调度器”，而是强调可观测、可决策、可执行、可回滚和可解释。

## 4. 质量门禁

最新 SP4 preflight 质量门禁：

- 2026-07-26 在 `/root/EulerPilot-closeout` 运行缩短版 preflight：`29/29 P0` 通过，已覆盖配置消费审计、benchmark baseline/randomized block、PolicyEngine 事务模型、ResourceControl rollback、SCX loader ownership、Security fail-closed 和 evidence validate-run 正负 fixture。
- 旧 `reports/final_quality_gate_20260720-stage3-performance.log` 保留为历史记录，不再作为 v6 封版最终门禁。

最终 evidence：

```text
entries=41
missing_required=0
warnings=8
```

## 5. 可提交边界

已支撑：

- 统一 Agent / Skills / Policy Engine 架构
- `cgroup v2` 稳定后端与 `sched_ext/scx` 增强后端
- Resource / Network / Security 三方向 OS Agent 扩展
- Kubernetes 旁路隔离验证
- Web Console 可视化演示
- 41 条 evidence compact，其中 8 条历史性能证据被 status override 标记为 provisional/invalid

仍需人工补充：

- 正式 8-10 分钟演示视频文件或公开链接。
- 完成 Candidate Gate、Formal Artifact Gate、修正基线后的正式随机化实验和 release bundle 校验。
