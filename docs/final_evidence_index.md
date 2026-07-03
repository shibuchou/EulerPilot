# EulerPilot 最终证据索引

更新时间：`2026-07-03`

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

服务联动 Security anomaly 证据：

- 121：`results/security_policy/anomaly-rules-20260703-121-v4`
- 122：`results/security_policy/anomaly-rules-20260703-122-v2`
- 关键文件：`security_policy_events.anomaly-rules.jsonl`、`anomaly_event_summary.txt`、`summary.txt`、`report.md`

Credential 生命周期 anomaly 证据：

- 121：`results/security_policy/credential-anomaly-20260703-121-v3`
- 122：`results/security_policy/credential-anomaly-20260703-122-v3`
- 关键文件：`security_policy_events.credential-anomaly.jsonl`、`security_policy_events.credential-hits.jsonl`、`anomaly_event_summary.txt`、`summary.txt`、`report.md`

验证入口：

```bash
sudo tests/integration/test_policy_engine_security_network_resource.sh
sudo tests/integration/test_policy_engine_security_network_resource.sh --repeat 10
```

双机已通过结果目录：

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


## v3.2 真实 runtime / Kubernetes Pod 证据

v3.2 计划：`docs/next_phase_plan_v3_2.md`

2026-06-30 Podman 真实 runtime 已转 pass：

- 121 Podman runtime readiness：`results/resource_control/runtime-readiness-20260630-podman-121`，`container_runtime_ready=1`、`kubernetes_ready=0`
- 122 Podman runtime readiness：`results/resource_control/runtime-readiness-20260630-podman-122`，`container_runtime_ready=1`、`kubernetes_ready=0`
- 121 fake runtime target 回归：`results/resource_control/runtime-target-20260630-113310`
- 122 fake runtime target 回归：`results/resource_control/runtime-target-20260630-113354`
- 121 real Podman container target：`results/resource_control/real-runtime-target-20260630-podman-121-final2`
- 122 real Podman container target：`results/resource_control/real-runtime-target-20260630-podman-122-final2`

2026-06-30 k3s Kubernetes lab 已转 pass：

- 121 k3s readiness：`results/resource_control/runtime-readiness-20260630-k3s-121`，`container_runtime_ready=1`、`kubernetes_ready=1`
- 122 k3s readiness：`results/resource_control/runtime-readiness-20260630-k3s-122`，`container_runtime_ready=1`、`kubernetes_ready=1`
- 121 real Pod cgroup target：`results/resource_control/real-pod-target-20260630-k3s-121-v2`
- 122 real Pod cgroup target：`results/resource_control/real-pod-target-20260630-k3s-122-v1`
- 121 real Pod host veth QoS：`results/network_policy/real-pod-veth-qos-20260630-k3s-121-v2`
- 122 real Pod host veth QoS：`results/network_policy/real-pod-veth-qos-20260630-k3s-122-v1`
- 121 real Pod host veth XDP ICMP/TCP/UDP + UDP tuple：`results/network_policy/real-pod-veth-xdp-20260703-k3s-121-tuple-v1`
- 122 real Pod host veth XDP ICMP/TCP/UDP + UDP tuple：`results/network_policy/real-pod-veth-xdp-20260703-k3s-122-tuple-v1`
- 121 isolated-veth XDP ICMP/TCP/UDP + UDP tuple：`results/network_policy/xdp-20260703-121-fields-v1`
- 122 isolated-veth XDP ICMP/TCP/UDP + UDP tuple：`results/network_policy/xdp-20260703-122-fields-v1`
- 121 Security credential anomaly：`results/security_policy/credential-anomaly-20260703-121-v3`
- 122 Security credential anomaly：`results/security_policy/credential-anomaly-20260703-122-v3`
- 121 real Pod Policy Engine 联动：`results/policy_engine/real-pod-security-network-resource-20260630-k3s-121-v1`
- 122 real Pod Policy Engine 联动：`results/policy_engine/real-pod-security-network-resource-20260630-k3s-122-v1`
- 121 v3.2 k3s 后质量门禁：`reports/final_quality_gate_20260630-v32-real-pod-policy-121.log`

当前结论：Docker/Podman/k3s/kubectl 包已安装；Docker 18.09 daemon 在当前 cgroup v2 环境下因 `Devices cgroup isn't mounted` 不作为主验证 runtime，Podman 4.9.4 与 k3s v1.24.2 可用。两台机器均已使用本地 `localhost/eulerpilot-busybox:latest` 镜像完成真实 container cgroup、真实 Kubernetes Pod cgroup、真实 Pod host veth QoS 写入/限速/rollback 验证，并已把 `policy_engine` 第二条跨 Skill 联动扩展到真实 Pod cgroup + host veth。为避免 Docker Hub 依赖，k3s 使用本地构造的 `docker.io/rancher/mirrored-pause:3.6` pause 镜像。

SP4 环境探测：

- SP4 check 121：`results/resource_control/sp4-env-20260630-101422-121.log`
- SP4 check 122：`results/resource_control/sp4-env-20260630-101422-122.log`

历史 blocked / iSulad 准备证据保留：

- 121 isula readiness：`results/resource_control/runtime-readiness-20260630-isula-check-121`
- 122 isula readiness：`results/resource_control/runtime-readiness-20260630-isula-check-122`
- 121 isula real runtime blocked：`results/resource_control/real-runtime-target-20260630-isula-check-121`
- 122 isula real runtime blocked：`results/resource_control/real-runtime-target-20260630-isula-check-122`
- 121 v3.1 回归：`results/policy_engine/security-network-resource-20260630-102629`

121 在 k3s/Pod veth 代码接入后再次通过 `scripts/final_quality_gate.sh`：21/21 P0、100 轮 smoke、5 轮 doctor 均通过。
## 后置事项

- SP4 验证不作为 v3.1 完成条件，准备文档为 `docs/sp4_validation_plan.md`，检查脚本为 `scripts/check_sp4_env.sh`。
- 真实 Kubernetes Pod target、Pod host veth QoS、Pod host veth XDP 与真实 Pod Policy Engine 跨 Skill 联动均已转 pass；isolated-veth 与 real Pod host veth XDP 均已补齐 ICMP/TCP/UDP + UDP tuple 四规则和 per-rule 字段证据；Security 已补齐 `credential_churn` 生命周期 anomaly 双机证据。下一优先级是 cred_transfer/cred_alloc_blank 等更深 credential hook 评估和最终证据压缩。
