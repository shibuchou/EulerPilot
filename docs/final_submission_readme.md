# EulerPilot 最终提交说明

更新时间：`2026-07-24`

## 1. 当前提交材料已经具备什么

当前项目已经具备：

- 可编译、可运行的 C++ Agent 与 eBPF 程序。
- `cgroup v2` 稳定执行后端和 `sched_ext/scx` 增强执行后端。
- Resource Control、Network Policy/QoS/XDP、Security Policy/LSM/anomaly 三方向 OS Agent 扩展。
- Policy Engine 跨 Skill 联动、事务回滚、AuditBus 和 ActionJournal。
- Redis/Nginx RUNS=10 frozen-code、压力梯度、静态 vs 动态、throughput-first、mixed-adaptive、Agent overhead。
- SP4 主验证、K8s 旁路隔离验证、Web Console 展示。
- 41 条 final evidence compact，strict 缺失 0、警告 0。
- 最终质量门禁 22/22 P0、100 轮 smoke、5 轮 doctor 通过。

这意味着：

> 当前提交材料已经覆盖实现、实验、证据、演示、报告和答辩六个层面。

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

- `results/final/redis-scx-compare-20260724-tested-2541464-runs10`
- `results/final/nginx-scx-compare-20260724-tested-2541464-runs10`
- `results/final/redis-pressure-gradient-20260708-153811`
- `results/final/redis-static-vs-agent-20260724-tested-2541464-runs10`
- `results/final/throughput-first-20260724-tested-2541464-runs10`
- `results/final/mixed-adaptive-20260724-tested-2541464-runs10-lite`
- `results/final/agent-overhead-20260724-tested-2541464-runs10`
- `results/k8s/sp4-validation-20260708-023552`
- `reports/final_figures`
- `reports/dashboard`

## 4. 当前还需要人工完成的最后事项

1. 按 `docs/demo_video_recording_script.md` 录制 8-10 分钟正式演示视频。
2. 在提交平台、最终报告或提交包中填写视频文件路径或公开链接。
3. release/tag 前再次运行：

```bash
python3 scripts/collect_final_evidence.py --strict
scripts/final_quality_gate.sh
git status --short
```

## 5. 当前可直接对外说明的状态

> EulerPilot 已完成面向 openEuler 的自适应资源管控 Agent 工程闭环，覆盖 workload 感知、Policy Engine 决策、Resource/Network/Security Skills、cgroup v2 与 sched_ext/scx 双后端、Kubernetes 旁路验证、Web Console 和最终证据链。当前仍需补充正式演示视频后即可进入最终 release/tag。
