# reports

作用：存放报告草稿、质量门禁输出、图表和面向提交/答辩的汇总材料。

## 关键内容

- `final_figures/`：最终报告候选图表。
- `dashboard/`：静态 dashboard 输出。
- `final_quality_gate_121.tap`：121 上已有质量门禁记录。
- `technical_report_*.md`：技术报告阶段稿。

## 当前完成状态

- 已有 Redis/Nginx/sched_ext 相关图表和质量门禁记录。
- v2.1 后续需要新增：
  - Network Policy 事件与实验报告。
  - Security Agent 事件与实验报告。
  - Resource Control CPU/Memory/IO 实验报告。
  - 跨 Agent 联动事件报告。

## 维护规则

- 报告必须能追溯到 `results/` 中的原始数据。
- 新增图表需要说明数据来源、生成脚本和结论边界。
- 不应在报告中掩盖无收益或负收益场景，需要解释边界。
