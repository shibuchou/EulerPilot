# Redis 主候选实验结果摘要

## 当前主候选参数

当前阶段 Redis 主候选实验参数如下：

```text
latency_weight = 1000
background_weight = 20

cpu_psi_threshold = 0.05
latency_wait_threshold_ns = 500000
background_runtime_threshold_ns = 2500000
```

对应结果目录：

```text
/root/EulerPilot/results/reports/redis-20260604-201635
```

## 实验边界

- `redis-server` 作为被保护服务，进入 `/eulerpilot/latency`
- `stress-ng` 作为后台干扰 workload，进入 `/eulerpilot/background`
- `redis-benchmark` 仅作为压测客户端，保持在默认根组，不参与分类控制
- `cpu.weight` 的解释作用域限定在同一父级 `/eulerpilot` 下的 sibling cgroup 之间

## 当前结果概览

基于当前 `compare_summary_avg.csv`：

- `GET`
  - RPS：相对 `default_noisy` 下降
  - P99：相对 `default_noisy` 改善
- `INCR`
  - RPS：相对 `default_noisy` 提升
  - P99：相对 `default_noisy` 改善
- `PING_INLINE`
  - RPS：相对 `default_noisy` 提升
  - P99：相对 `default_noisy` 改善
- `SET`
  - RPS：相对 `default_noisy` 轻微下降
  - P99：相对 `default_noisy` 处于可接受区间

## 当前阶段结论

当前主候选参数已经比 `background_weight = 5 / 10` 时更加平衡。它不再一味强调激进抑制后台负载，而是在保持 Redis 尾延迟改善的同时，尽量减少吞吐损失。

因此，当前可以将该组参数作为：

- 当前阶段 Redis 主实验候选参数
- 后续技术报告正文的优先引用对象

## 为什么当前选择这一组

当前不再以“某一项指标最漂亮”为目标，而是优先选择：

- 证据链完整
- 逻辑可解释
- 大多数关键操作不过度退化
- 结果方向稳定

相比更激进的 `background_weight = 5` 与更保守的其他组合，当前这组参数更符合“延迟优先但避免过控”的阶段目标。

## 结论边界

说明：本文件主要保留 Redis 早期候选阶段的阶段性判断；当前最新正式候选结果应以：

- `/root/EulerPilot/results/final/redis-scx-compare-20260612-191543`
- `/root/EulerPilot/docs/final_results_summary.md`

为准。

当前结果仍然是“阶段候选”，不是最终定稿结论：

- 早期阶段仍需继续验证更多轮次
- 早期阶段仍需补充第二业务线，证明方案不是 Redis 特化
- 当前项目后续正式结论已转入多轮候选结果和最终总结文档
