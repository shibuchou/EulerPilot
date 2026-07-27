# 提交清单

更新时间：`2026-07-26`

## 代码

- `agent/`：C++ Agent Runtime、Skill Manager、Executor、Metrics、Policy Engine。
- `agent/src/builtin_skills/`：Resource、Network、Security、Policy Engine 等内置 Skill。
- `bpf/`：workload observer、Network Policy、Network QoS、XDP、Security Policy。
- `sched/`：`sched_ext/scx` 调度器与 `scx_eulerpilot`。
- `configs/`：Agent、Skills、Policy Engine、final evidence manifest。
- `scripts/`：环境检查、回滚、质量门禁、证据收集、图表与报告生成。
- `tests/`：集成测试、benchmark、C++ 单元测试。
- `web_console/`：Evidence-first 旁路展示控制台。

## 文档

- `README.md`
- `docs/project_brief.md`
- `docs/architecture.md`
- `docs/final_submission_guide.md`
- `docs/final_report_submission.md`
- `docs/final_evidence_index.md`
- `docs/defense_final.md`
- `docs/defense_qa.md`
- `docs/demo_final_runbook.md`
- `docs/demo_video_recording_script.md`

## 结果与证据

- `configs/final_evidence_manifest.json`
- `reports/final_evidence_compact.md`
- `reports/final_evidence_compact.json`
- `reports/final_figures/`
- `reports/dashboard/`
- `results/final/`
- `results/policy_engine/`
- `results/network_policy/`
- `results/security_policy/`
- `results/resource_control/`
- `results/k8s/sp4-validation-20260708-023552/`

当前 v6 evidence compact 口径：

```text
entries=41
missing_required=0
warnings=8
```

8 条 warning 来自旧 SP4 RUNS=10 证据降级，不是文件缺失。正式 release 需要 `--validate-release` 在 formal artifact 重跑后通过。

## 现场演示

- `web_console/scripts/run_console.sh`
- `demo/demo_all_final.sh`
- `scripts/final_quality_gate.sh`
- `docs/demo_final_runbook.md`
- `docs/demo_video_recording_script.md`

## 仓库入口

- GitHub：`https://github.com/shibuchou/EulerPilot`
- GitLink：`https://gitlink.org.cn/HxQj0tp0pG/mxoedzsyzygka`

## 未放入仓库的材料

- 正式演示视频文件尚未交付。提交前需要录制 8-10 分钟视频，并在最终提交平台填写视频路径或公开链接。
