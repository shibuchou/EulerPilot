# Redis 抗干扰实验报告

## 报告来源

- 结果目录：`/root/EulerPilot/results/reports/redis-20260604-163726`
- 对比汇总文件：`/root/EulerPilot/results/reports/redis-20260604-163726/compare_summary_avg.csv`

## 结果概览

本报告基于多轮 `baseline/default_noisy/active_noisy` 对比结果自动生成。

## 汇总表

| 测试项 | default noisy RPS均值 | active noisy RPS均值 | RPS变化 | default noisy P99均值(ms) | active noisy P99均值(ms) | P99变化 | RPS标准差(active) | P99标准差(active) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| GET | 38095.240 | 32000.000 | -16.00% | 0.303 | 0.239 | -21.12% | 0.000 | 0.000 |
| INCR | 33333.330 | 36363.640 | 9.09% | 0.319 | 0.295 | -7.52% | 0.000 | 0.000 |
| PING_INLINE | 32000.000 | 33333.330 | 4.17% | 0.271 | 0.255 | -5.90% | 0.000 | 0.000 |
| SET | 34782.610 | 32000.000 | -8.00% | 0.375 | 0.311 | -17.07% | 0.000 | 0.000 |

## 自动结论

- `GET`：相对 `default_noisy`，RPS 下降 -16.00%，P99 改善 -21.12%。
- `INCR`：相对 `default_noisy`，RPS 提升 9.09%，P99 改善 -7.52%。
- `PING_INLINE`：相对 `default_noisy`，RPS 提升 4.17%，P99 改善 -5.90%。
- `SET`：相对 `default_noisy`，RPS 下降 -8.00%，P99 改善 -17.07%。

## Agent 证据摘录

- 当前未在 active agent 快照中提取到 `applied=yes` 的 redis/stress-ng 关键证据。

## CPU PSI 摘要

- `pre`: some avg10=0.00 avg60=0.00 avg300=0.02 total=67256347
- `pre`: full avg10=0.00 avg60=0.00 avg300=0.00 total=0
- 当前阶段 PSI 阈值仍采用静态阈值方案，后续计划演进为 baseline 自适应阈值。

## 后续建议

- 继续增加重复轮数，降低实验波动。
- 继续优化 Redis 与 stress-ng 的控制策略和目标识别规则。
- 补充更多执行证据和图表化材料。
