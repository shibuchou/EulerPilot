# EulerPilot 最终证据索引

更新时间：`2026-06-29`

本文作为答辩和最终提交前的证据入口，集中索引 EulerPilot 当前已经具备的设计文档、测试脚本、结果目录和演示材料。日期快照文档不反复覆盖；滚动进度以 `docs/progress_status.md` 为准。

## 核心入口

- 项目总览：`README.md`
- 赛题宣讲参考：`docs/contest_briefing_reference.md`
- 架构说明：`docs/architecture.md`
- 当前进度看板：`docs/progress_status.md`
- 提交检查表：`docs/submission_checklist.md`
- v3.1 开始状态：`docs/v3_1_start_status_20260629.md`
- v3.1 开始仓库快照：`reports/v3_1_start_repo_status_20260629-1859.log`

## Skill 证据

- CPU Scheduling / sched_ext：`docs/final_results_summary.md`、`docs/final_report_submission.md`
- Network Policy / QoS / XDP：`docs/network_policy_skill.md`
- Security Policy / LSM / anomaly：`docs/security_policy_skill.md`
- Resource Control / CPU+Memory+IO：`docs/resource_control_skill.md`
- Policy Engine 跨 Skill 联动：`docs/policy_engine_skill.md`

## v3.1 关键链路

第二条跨 Skill 联动链路：

```text
security_policy burst_connect anomaly
  -> policy_engine decision
  -> resource_control demo_cgroup cpu.max / memory.high
  -> network_qos lab_netdev tc/tbf 2mbit
  -> AuditBus + ActionJournal
  -> stop rollback
```

配置入口：

- `configs/policy_engine_security_network_resource.yaml`
- `configs/policy_engine_security_network_resource.skills.yaml`

验证入口：

```bash
sudo tests/integration/test_policy_engine_security_network_resource.sh
sudo tests/integration/test_policy_engine_security_network_resource.sh --repeat 10
```

121 已通过结果目录：

- 121：`results/policy_engine/security-network-resource-20260629-214952`
- 122：`results/policy_engine/security-network-resource-20260629-215950`

## 结果文件清单

v3.1 结果目录至少应包含：

- `before_rate.txt`
- `after_rate.txt`
- `tc_qdisc_before.txt`
- `tc_qdisc_after.txt`
- `tc_qdisc_rollback.txt`
- `policy_engine_events.jsonl`
- `security_policy_events.jsonl`
- `network_policy_events.jsonl`
- `resource_control_events.jsonl`
- `action_journal.jsonl`
- `summary.txt`
- `report.md`

## 演示入口

一键演示脚本：

```bash
sudo demo/demo_all_final.sh --mode live
demo/demo_all_final.sh --mode offline
sudo demo/demo_all_final.sh --cleanup
```

演示说明：`docs/demo_final_runbook.md`

演示顺序：

```text
check_env
-> list-skills
-> validate-config
-> doctor-skills
-> CPU sched_ext 结果展示
-> Network QoS/XDP
-> Security anomaly/LSM
-> Resource CPU/Memory/IO
-> Policy Engine cross-skill v1/v2
-> rollback/status
```


## v3.2 启动证据

v3.2 计划：`docs/next_phase_plan_v3_2.md`

2026-06-30 真实 runtime/Kubernetes readiness 证据：

- 121：`results/resource_control/runtime-readiness-20260630-1020-121`
- 122：`results/resource_control/runtime-readiness-20260630-1020-122`
- 121 real runtime：`results/resource_control/real-runtime-target-20260630-1020-121`
- 122 real runtime：`results/resource_control/real-runtime-target-20260630-1020-122`
- 121 real Pod：`results/resource_control/real-pod-target-20260630-1020-121`
- 122 real Pod：`results/resource_control/real-pod-target-20260630-1020-122`
- SP4 check 121：`results/resource_control/sp4-env-20260630-101422-121.log`
- SP4 check 122：`results/resource_control/sp4-env-20260630-101422-122.log`

当前结论：121/122 均仍缺 container runtime 与 Kubernetes lab；v3.2 第一阶段先补 openEuler iSulad/isula 兼容，再在用户允许安装或提供 runtime 后把 blocked 转为 pass。
v3.2 iSulad/isula 回归证据：

- 121 TargetResolver + fake runtime target：`results/resource_control/runtime-target-20260630-102314`
- 122 TargetResolver + fake runtime target：`results/resource_control/runtime-target-20260630-102535`
- 121 isula readiness：`results/resource_control/runtime-readiness-20260630-isula-check-121`
- 122 isula readiness：`results/resource_control/runtime-readiness-20260630-isula-check-122`
- 121 isula real runtime blocked：`results/resource_control/real-runtime-target-20260630-isula-check-121`
- 122 isula real runtime blocked：`results/resource_control/real-runtime-target-20260630-isula-check-122`
- 121 v3.1 回归：`results/policy_engine/security-network-resource-20260630-102629`

121 在 iSulad/isula 代码接入后再次通过 `scripts/final_quality_gate.sh`，仍为 21/21 P0、100 轮 smoke、5 轮 doctor 通过。

## 后置事项

- SP4 验证不作为 v3.1 完成条件，准备文档为 `docs/sp4_validation_plan.md`，检查脚本为 `scripts/check_sp4_env.sh`。
- Kubernetes/真实 runtime 不作为 v3.1 完成条件，但作为 v3.2 第一优先级，不得从最终争奖路线中删除。