# Nginx 抗干扰实验报告

## 报告来源

- 结果目录：`/root/EulerPilot/results/reports/nginx-scx-smoke-20260727-100645`

## 汇总表

| 阶段 | Requests/sec | Avg Latency | P99 Latency |
| --- | ---: | ---: | ---: |
| baseline | 11966.20 | 2.56ms | 5.72ms |
| default_noisy | 11966.20 | 2.56ms | 5.72ms |
| active_noisy | 13177.18 | 2.67ms | 16.82ms |

## 自动结论

- 相对 `default_noisy`，`active_noisy` 的吞吐变化为 `10.12%`。
- `default_noisy` 的 P99 为 `5.72ms`，`active_noisy` 的 P99 为 `16.82ms`。

## Agent 证据摘录

- 当前未提取到 Nginx / stress-ng 的 `applied=yes` 关键证据。