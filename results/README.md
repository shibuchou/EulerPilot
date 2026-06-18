# Results 目录说明

实验结果输出目录：

- `csv/`：原始指标和汇总表
- `figures/`：图表
- `reports/`：Markdown/PDF 实验报告

## 当前阶段

- `reports/mixed-<timestamp>/`：保存实验框架验证结果
- `reports/redis-<timestamp>/`：保存 Redis 正式实验结果
- `reports/profile-sweep-<timestamp>/`：保存 `cpu.weight` 参数扫描索引
- `reports/psi-threshold-sweep-<timestamp>/`：保存 PSI 阈值扫描索引

一次 Redis 正式实验当前至少包含：

- `compare_summary_avg.csv`
- `report.md`
- `summary.md`
- `run-*/compare_summary.csv`
- `run-*/active_noisy_agent_snapshot.txt`
- 前后系统快照
