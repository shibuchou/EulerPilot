# Redis 抗干扰实验报告

## 报告来源

- 结果目录：`/root/EulerPilot/results/reports/redis-20260603-210305`
- 对比汇总文件：`/root/EulerPilot/results/reports/redis-20260603-210305/compare_summary_avg.csv`

## 结果概览

本报告基于 `compare_summary_avg.csv` 自动生成，用于比较以下三种阶段：

- `baseline`：无后台干扰
- `default_noisy`：有后台干扰，但不启用 EulerPilot 主动控制
- `active_noisy`：有后台干扰，并启用 EulerPilot 主动控制

## 汇总表

| 测试项 | baseline RPS | default noisy RPS | active noisy RPS | default->active RPS变化 | baseline P99(ms) | default noisy P99(ms) | active noisy P99(ms) | default->active P99变化 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| GET | 28571.430 | 32258.060 | 32258.060 | 0.00% | 0.383 | 0.295 | 0.343 | 16.27% |
| INCR | 29411.760 | 37037.040 | 30303.030 | -18.18% | 0.399 | 0.239 | 0.391 | 63.60% |
| PING_INLINE | 26315.790 | 33333.340 | 26315.790 | -21.05% | 0.519 | 0.263 | 0.455 | 73.00% |
| SET | 30303.030 | 32258.060 | 33333.340 | 3.33% | 0.367 | 0.327 | 0.327 | 0.00% |

## 自动结论

- `SET`：相对 `default_noisy`，RPS 提升 3.33%，P99 变差 0.00%。

## 后续建议

- 继续增加重复轮数，降低实验波动。
- 补充 Agent 分类证据和 CPU PSI 摘要。
- 继续优化 Redis 与 stress-ng 的控制策略和目标识别规则。
