# EulerPilot 最终提交说明

更新时间：`2026-07-26`

## 1. 当前提交材料已经具备什么

当前项目已经具备：

- 可编译、可运行的 C++ Agent 与 eBPF 程序。
- `cgroup v2` 稳定执行后端和 `sched_ext/scx` 增强执行后端。
- Resource Control、Network Policy/QoS/XDP、Security Policy/LSM/anomaly 三方向 OS Agent 扩展。
- Policy Engine 跨 Skill 联动、事务回滚、AuditBus 和 ActionJournal。
- Redis/Nginx RUNS=10 historical/provisional、压力梯度、静态 vs 动态、Agent overhead 历史趋势证据。
- Throughput-first、mixed-adaptive 历史结果已按 v6 复审标为 invalid historical。
- SP4 主验证、K8s 旁路隔离验证、Web Console 展示。
- 41 条 evidence compact，缺失 0、预期警告 8；警告来自旧证据降级。
- v6 缩短版 preflight 质量门禁 29/29 P0 通过；最终质量门禁需绑定同一 candidate SHA 和 formal artifact 重跑。

这意味着：

> 当前提交材料已经覆盖实现、历史实验、证据、演示、报告和答辩六个层面；正式性能收益与 release/tag 仍需 v6 formal artifact 重跑后确认。

## 2. 当前建议优先使用的文档

### 2.1 总体说明

- `README.md`
- `docs/project_brief.md`
- `docs/architecture.md`

### 2.2 正文主稿

- `docs/final_report_submission.md`
- `docs/final_submission_guide.md`

### 2.3 结果与证据

- `docs/final_evidence_index.md`
- `reports/final_evidence_compact.md`
- `docs/final_results_summary.md`

### 2.4 答辩材料

- `docs/defense_final.md`
- `docs/defense_summary.md`
- `docs/defense_slides_outline.md`
- `docs/defense_qa.md`
- `docs/final_talk_script.md`

### 2.5 演示材料

- `docs/demo_final_runbook.md`
- `docs/demo_video_recording_script.md`
- `web_console/README.md`
- `demo/demo_all_final.sh`

### 2.6 提交索引

- `submission/README.md`
- `submission/submission_manifest.md`
- `submission/evidence_summary.md`
- `submission/build_and_run.md`
- `submission/known_limits.md`

## 3. 当前建议优先使用的结果目录

- `results/final/redis-scx-compare-20260724-tested-2541464-runs10`：historical/provisional
- `results/final/nginx-scx-compare-20260724-tested-2541464-runs10`：historical/provisional
- `results/final/redis-pressure-gradient-20260724-tested-2541464-runs3`：historical/provisional
- `results/final/redis-static-vs-agent-20260724-tested-2541464-runs10`：historical/provisional
- `results/final/throughput-first-20260724-tested-2541464-runs10`：invalid historical
- `results/final/mixed-adaptive-20260724-tested-2541464-runs10-lite`：invalid historical
- `results/final/agent-overhead-20260724-tested-2541464-runs10`：historical/provisional
- `results/k8s/sp4-validation-20260708-023552`
- `reports/final_figures`
- `reports/dashboard`

## 4. 当前还需要人工完成的最后事项

1. 按 `docs/demo_video_recording_script.md` 录制 8-10 分钟正式演示视频。
2. 在提交平台、最终报告或提交包中填写视频文件路径或公开链接。
3. release/tag 前按 v6 流程运行：

```bash
python3 scripts/collect_final_evidence.py --validate-release
scripts/final_quality_gate.sh
git status --short
```

## 5. 当前可直接对外说明的状态

> EulerPilot 已完成面向 openEuler 的自适应资源管控 Agent 主体工程闭环，覆盖 workload 感知、Policy Engine 决策、Resource/Network/Security Skills、cgroup v2 与 sched_ext/scx 双后端、Kubernetes 旁路验证、Web Console 和证据链。当前仍需完成 Candidate Gate、Formal Artifact Gate、修正 baseline 后的正式实验和演示视频，之后才能进入最终 release/tag。
