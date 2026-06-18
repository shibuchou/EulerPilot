# Redis 抗干扰实验报告

## 结果概览

本报告基于 `compare_summary_avg.csv` 自动生成，用于比较以下三种阶段：

- `baseline`：无后台干扰
- `default_noisy`：有后台干扰，但不启用 EulerPilot 主动控制
- `active_noisy`：有后台干扰，并启用 EulerPilot 主动控制

## 汇总表

| 测试项 | baseline RPS | default noisy RPS | active noisy RPS | baseline P99(ms) | default noisy P99(ms) | active noisy P99(ms) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| GET | 31250.000 | 34482.760 | 31250.000 | 0.279 | 0.319 | 0.343 |
| INCR | 40000.000 | 33333.340 | 34482.760 | 0.263 | 0.263 | 0.279 |
| PING_INLINE | 30303.030 | 31250.000 | 32258.060 | 0.319 | 0.303 | 0.335 |
| SET | 33333.340 | 31250.000 | 34482.760 | 0.335 | 0.407 | 0.271 |

## 说明

- 该报告为自动汇总版本，主要用于开发阶段快速比较趋势。
- 后续可以在此基础上继续补充多轮标准差、提升百分比、Agent 分类证据和 CPU PSI 变化图。
