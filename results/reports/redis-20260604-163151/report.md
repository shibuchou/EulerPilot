# Redis 抗干扰实验报告

## 报告来源

- 结果目录：`/root/EulerPilot/results/reports/redis-20260604-163151`
- 对比汇总文件：`/root/EulerPilot/results/reports/redis-20260604-163151/compare_summary_avg.csv`

## 结果概览

本报告基于多轮 `baseline/default_noisy/active_noisy` 对比结果自动生成。

## 汇总表

| 测试项 | default noisy RPS均值 | active noisy RPS均值 | RPS变化 | default noisy P99均值(ms) | active noisy P99均值(ms) | P99变化 | RPS标准差(active) | P99标准差(active) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| GET | 38461.540 | 32258.060 | -16.13% | 0.279 | 0.303 | 8.60% | 0.000 | 0.000 |
| INCR | 37037.040 | 33333.340 | -10.00% | 0.263 | 0.343 | 30.42% | 0.000 | 0.000 |
| PING_INLINE | 33333.340 | 32258.060 | -3.23% | 0.255 | 0.351 | 37.65% | 0.000 | 0.000 |
| SET | 40000.000 | 32258.060 | -19.35% | 0.359 | 0.383 | 6.69% | 0.000 | 0.000 |

## 自动结论

- 当前自动汇总尚未观察到稳定的 active 优势，需要继续调参和重复实验。

## Agent 证据摘录

- 当前未在 active agent 快照中提取到 `applied=yes` 的 redis/stress-ng 关键证据。

## CPU PSI 摘要

- `pre`: some avg10=0.00 avg60=0.00 avg300=0.14 total=66965858
- `pre`: full avg10=0.00 avg60=0.00 avg300=0.00 total=0
- 当前阶段 PSI 阈值仍采用静态阈值方案，后续计划演进为 baseline 自适应阈值。

## 后续建议

- 继续增加重复轮数，降低实验波动。
- 继续优化 Redis 与 stress-ng 的控制策略和目标识别规则。
- 补充更多执行证据和图表化材料。
