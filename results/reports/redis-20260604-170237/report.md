# Redis 抗干扰实验报告

## 报告来源

- 结果目录：`/root/EulerPilot/results/reports/redis-20260604-170237`
- 对比汇总文件：`/root/EulerPilot/results/reports/redis-20260604-170237/compare_summary_avg.csv`

## 结果概览

本报告基于多轮 `baseline/default_noisy/active_noisy` 对比结果自动生成。

## 汇总表

| 测试项 | default noisy RPS均值 | active noisy RPS均值 | RPS变化 | default noisy P99均值(ms) | active noisy P99均值(ms) | P99变化 | RPS标准差(active) | P99标准差(active) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| GET | 29629.630 | 33333.330 | 12.50% | 0.295 | 0.279 | -5.42% | 0.000 | 0.000 |
| INCR | 34782.610 | 33333.330 | -4.17% | 0.455 | 0.335 | -26.37% | 0.000 | 0.000 |
| PING_INLINE | 29629.630 | 33333.330 | 12.50% | 0.279 | 0.367 | 31.54% | 0.000 | 0.000 |
| SET | 34782.610 | 33333.330 | -4.17% | 0.407 | 0.343 | -15.72% | 0.000 | 0.000 |

## 自动结论

- `GET`：相对 `default_noisy`，RPS 提升 12.50%，P99 改善 -5.42%。
- `INCR`：相对 `default_noisy`，RPS 下降 -4.17%，P99 改善 -26.37%。
- `PING_INLINE`：相对 `default_noisy`，RPS 提升 12.50%，P99 变差 31.54%。
- `SET`：相对 `default_noisy`，RPS 下降 -4.17%，P99 改善 -15.72%。

## Agent 证据摘录

- 当前未在 active agent 快照中提取到 `applied=yes` 的 redis/stress-ng 关键证据。

## CPU PSI 摘要

- `pre`: some avg10=0.00 avg60=0.00 avg300=0.03 total=70487394
- `pre`: full avg10=0.00 avg60=0.00 avg300=0.00 total=0
- 当前阶段 PSI 阈值仍采用静态阈值方案，后续计划演进为 baseline 自适应阈值。

## 后续建议

- 继续增加重复轮数，降低实验波动。
- 继续优化 Redis 与 stress-ng 的控制策略和目标识别规则。
- 补充更多执行证据和图表化材料。
