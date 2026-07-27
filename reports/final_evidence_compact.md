# EulerPilot 最终证据压缩报告

生成时间：`2026-07-27T11:34:56+08:00`
清单：`configs/final_evidence_manifest.json`
状态覆盖：`evidence/evidence_status_overrides.json`，覆盖条目 `8`

本报告由 `scripts/collect_final_evidence.py` 根据白名单清单生成，用于把分散的双机结果压缩成答辩入口。它不会递归扫描全部 `results/`，缺失项会显式列出；若 `evidence/evidence_status_overrides.json` 将旧证据标为 provisional 或 invalid，strict 模式会阻止其作为最终正向证据。

## 总览

| 指标 | 值 |
| --- | --- |
| 清单条目 | 42 |
| 必需缺失 | 0 |
| 带警告条目 | 0 |
| Git 工作区额外状态 | 124 |

## quality_gate

| 状态 | 主机 | 名称 | 路径 | 摘要 |
| --- | --- | --- | --- | --- |
| pass | 121 | 121 final quality gate after engineering quality closeout | `reports/final_quality_gate_20260706-quality-121.log` | result=pass |
| pass | 123 | SP4 final quality gate after sched_ext workload validation | `reports/sp4/final_quality_gate_scx_workload_20260706-1214.log` | result=pass |
| pass | 123 | SP4 final quality gate after stage3 performance evidence closeout | `reports/final_quality_gate_20260720-stage3-performance.log` | result=pass |
| present | 123 | SP4 Agent overhead RUNS=10 formal artifact | `/root/eulerpilot-runs/7a99d87048f4f2040377354bfe0ce21401664642/formal-experiments/20260726-162050/agent-overhead-runs10` | agent_overhead_validity=pass |
| pass | 123 | SP4 v6 release candidate final quality gate | `reports/final_quality_gate_20260726-v6-sp4-final.log` | result=pass |
| present | 121 | SP3 v6 capability matrix | `reports/gates/sp3_capability_matrix_20260726-v6-final.json` | - |
| present | 121 | SP3 v6 release candidate compatibility final gate | `reports/final_quality_gate_20260726-v6-sp3-final.log` | - |

## repo_status

| 状态 | 主机 | 名称 | 路径 | 摘要 |
| --- | --- | --- | --- | --- |
| present | local | v3.1 start repository snapshot | `reports/v3_1_start_repo_status_20260629-1859.log` | - |
| present | 123 | v6 formal artifact experiment suite summary | `reports/v6_formal_experiment_suite_20260726.md` | - |

## cpu_sched_ext

| 状态 | 主机 | 名称 | 路径 | 摘要 |
| --- | --- | --- | --- | --- |
| present | 123 | SP4 Redis sched_ext comparison RUNS=10 formal artifact | `/root/eulerpilot-runs/7a99d87048f4f2040377354bfe0ce21401664642/formal-experiments/20260726-162050/redis-scx-compare-runs10` | # Redis sched_ext 后端正式对照报告 / ## 运行信息 / - 结果目录：`/root/eulerpilot-runs/7a99d87048f4f2040377354bfe0ce21401664642/formal-experiments/20260726-162050/redis-scx-compare-runs10` |
| present | 123 | SP4 Nginx sched_ext comparison RUNS=10 formal artifact | `/root/eulerpilot-runs/7a99d87048f4f2040377354bfe0ce21401664642/formal-experiments/20260726-162050/nginx-scx-compare-runs10` | # Nginx sched_ext 后端正式对照报告 / ## 运行信息 / - 结果目录：`/root/eulerpilot-runs/7a99d87048f4f2040377354bfe0ce21401664642/formal-experiments/20260726-162050/nginx-scx-compare-runs10` |
| present | 123 | SP4 throughput-first RUNS=10 formal artifact | `/root/eulerpilot-runs/7a99d87048f4f2040377354bfe0ce21401664642/formal-experiments/20260726-162050/throughput-first-runs10` | throughput_first_validity=pass |
| present | 123 | SP4 mixed-adaptive RUNS=10 formal artifact | `/root/eulerpilot-runs/7a99d87048f4f2040377354bfe0ce21401664642/formal-experiments/20260726-162050/mixed-adaptive-runs10` | mixed_adaptive_validity=pass |
| present | 121 | Redis sched_ext comparison | `results/final/redis-scx-compare-20260612-191543` | # Redis sched_ext 正式对照 / - timestamp: 2026-06-12T19:28:10+08:00 / - runs: 5 |
| present | 121 | Nginx sched_ext comparison | `results/final/nginx-scx-compare-20260612-194018` | # Nginx sched_ext 正式对照 / - timestamp: 2026-06-12T19:52:03+08:00 / - runs: 5 |
| present | 123 | SP4 Redis PSI gate ACTIVE probe | `results/final/redis-scx-psi-probe-20260706-100857` | ﻿# Redis sched_ext PSI ACTIVE Probe / - timestamp: 2026-07-06T10:09:21+08:00 / - redis port: 6390 |

## network

| 状态 | 主机 | 名称 | 路径 | 摘要 |
| --- | --- | --- | --- | --- |
| pass | 121 | isolated-veth XDP field tuple 121 | `results/network_policy/xdp-20260703-121-fields-v1` | result=pass |
| pass | 122 | isolated-veth XDP field tuple 122 | `results/network_policy/xdp-20260703-122-fields-v1` | result=pass |
| pass | 121 | real Pod host veth XDP tuple 121 | `results/network_policy/real-pod-veth-xdp-20260703-k3s-121-tuple-v1` | result=pass<br>reason=real-pod-veth-xdp-attached-dropped-and-restored<br>kernel=6.6.0-132.0.0.111.oe2403sp3.x86_64<br>target_ref=lab_pod<br>host_veth=veth998e0158 |
| pass | 122 | real Pod host veth XDP tuple 122 | `results/network_policy/real-pod-veth-xdp-20260703-k3s-122-tuple-v1` | result=pass<br>reason=real-pod-veth-xdp-attached-dropped-and-restored<br>kernel=6.6.0-olk66-scx<br>target_ref=lab_pod<br>host_veth=vethc59976b2 |
| pass | 121 | real Pod host veth QoS 121 | `results/network_policy/real-pod-veth-qos-20260630-k3s-121-v2` | result=pass<br>reason=real-pod-veth-qos-applied-and-restored<br>kernel=6.6.0-132.0.0.111.oe2403sp3.x86_64<br>target_ref=lab_pod<br>host_veth=veth998e0158 |
| pass | 122 | real Pod host veth QoS 122 | `results/network_policy/real-pod-veth-qos-20260630-k3s-122-v1` | result=pass<br>reason=real-pod-veth-qos-applied-and-restored<br>kernel=6.6.0-olk66-scx<br>target_ref=lab_pod<br>host_veth=vethc59976b2 |

## security

| 状态 | 主机 | 名称 | 路径 | 摘要 |
| --- | --- | --- | --- | --- |
| pass | 121 | service anomaly rules 121 | `results/security_policy/anomaly-rules-20260703-121-v4` | result=pass<br>reason=service-linkage-anomaly-rules-observed<br>kernel=6.6.0-132.0.0.111.oe2403sp3.x86_64 |
| pass | 122 | service anomaly rules 122 | `results/security_policy/anomaly-rules-20260703-122-v2` | result=pass<br>reason=service-linkage-anomaly-rules-observed<br>kernel=6.6.0-olk66-scx |
| pass | 121 | anomaly process filter 121 | `results/security_policy/anomaly-process-filter-20260703-121-v1` | result=pass<br>reason=security-anomaly-process-filter-observed<br>kernel=6.6.0-132.0.0.111.oe2403sp3.x86_64 |
| pass | 122 | anomaly process filter 122 | `results/security_policy/anomaly-process-filter-20260703-122-v1` | result=pass<br>reason=security-anomaly-process-filter-observed<br>kernel=6.6.0-olk66-scx |
| pass | 121 | anomaly combo scope 121 | `results/security_policy/anomaly-combo-scope-20260703-121-v1` | result=pass<br>reason=security-anomaly-combo-scope-observed<br>kernel=6.6.0-132.0.0.111.oe2403sp3.x86_64<br>target_ref=scoped_python_etc |
| pass | 122 | anomaly combo scope 122 | `results/security_policy/anomaly-combo-scope-20260703-122-v1` | result=pass<br>reason=security-anomaly-combo-scope-observed<br>kernel=6.6.0-olk66-scx<br>target_ref=scoped_python_etc |
| pass | 121 | credential anomaly 121 | `results/security_policy/credential-anomaly-20260703-121-v4` | result=pass<br>reason=credential-churn-anomaly-observed<br>kernel=6.6.0-132.0.0.111.oe2403sp3.x86_64 |
| pass | 122 | credential anomaly 122 | `results/security_policy/credential-anomaly-20260703-122-v4` | result=pass<br>reason=credential-churn-anomaly-observed<br>kernel=6.6.0-olk66-scx |
| pass | 121 | credential deep hook evaluation 121 | `results/security_policy/credential-deep-hooks-20260703-121-v2` | result=pass<br>reason=deep-credential-hooks-configured-attached-and-scoped<br>kernel=6.6.0-132.0.0.111.oe2403sp3.x86_64<br>deep_hook_runtime_note=no-userland-hit-observed<br>lsm_cred_alloc_blank_attach=agent-started |
| pass | 122 | credential deep hook evaluation 122 | `results/security_policy/credential-deep-hooks-20260703-122-v2` | result=pass<br>reason=deep-credential-hooks-configured-attached-and-scoped<br>kernel=6.6.0-olk66-scx<br>deep_hook_runtime_note=no-userland-hit-observed<br>lsm_cred_alloc_blank_attach=agent-started |

## resource_control

| 状态 | 主机 | 名称 | 路径 | 摘要 |
| --- | --- | --- | --- | --- |
| pass | 121 | IO controller 121 | `results/resource_control/io-20260624-160008` | result=pass<br>io_max_pressure=253:0 rbps=max wbps=1048576<br>io_weight_pressure=default 50<br>limited_time_s=6.684 |
| pass | 122 | IO controller 122 | `results/resource_control/io-20260624-160208` | result=pass<br>io_max_pressure=253:0 rbps=max wbps=1048576<br>io_weight_pressure=default 50<br>limited_time_s=13.784 |
| pass | 121 | real Kubernetes Pod cgroup 121 | `results/resource_control/real-pod-target-20260630-k3s-121-v2` | result=pass<br>reason=real-pod-target-applied-and-restored<br>kernel=6.6.0-132.0.0.111.oe2403sp3.x86_64<br>target_ref=lab_pod<br>target_cgroup=/sys/fs/cgroup/kubepods/besteffort/podec49c0af-8ae7-462b-8a6d-9fef2b0b62b3 |
| pass | 122 | real Kubernetes Pod cgroup 122 | `results/resource_control/real-pod-target-20260630-k3s-122-v1` | result=pass<br>reason=real-pod-target-applied-and-restored<br>kernel=6.6.0-olk66-scx<br>target_ref=lab_pod<br>target_cgroup=/sys/fs/cgroup/kubepods/besteffort/pod03a42fb7-0c29-4c32-9dde-bd5367b9cfcc |

## policy_engine

| 状态 | 主机 | 名称 | 路径 | 摘要 |
| --- | --- | --- | --- | --- |
| pass | 121 | security to network and resource 121 | `results/policy_engine/security-network-resource-20260629-214952` | result=pass |
| pass | 122 | security to network and resource 122 | `results/policy_engine/security-network-resource-20260629-215950` | result=pass |
| pass | 121 | real Pod security to network and resource 121 | `results/policy_engine/real-pod-security-network-resource-20260630-k3s-121-v1` | result=pass<br>reason=real-pod-security-network-resource-applied-and-restored<br>kernel=6.6.0-132.0.0.111.oe2403sp3.x86_64<br>transaction_id=pe-v3-1-1-1782809635<br>policy_id=security_network_resource_real_pod_response |
| pass | 122 | real Pod security to network and resource 122 | `results/policy_engine/real-pod-security-network-resource-20260630-k3s-122-v1` | result=pass<br>reason=real-pod-security-network-resource-applied-and-restored<br>kernel=6.6.0-olk66-scx<br>transaction_id=pe-v3-1-1-1782809810<br>policy_id=security_network_resource_real_pod_response |

## kubernetes_validation

| 状态 | 主机 | 名称 | 路径 | 摘要 |
| --- | --- | --- | --- | --- |
| present | 123+k8s-master | SP4 Kubernetes isolated namespace smoke | `results/k8s/sp4-validation-20260708-023552` | # SP4 / Kubernetes 旁路验证报告 / 时间：`2026-07-08 10:35 CST` / ## 结论 |

## web_console

| 状态 | 主机 | 名称 | 路径 | 摘要 |
| --- | --- | --- | --- | --- |
| present | 123 | SP4 Web Console whitelist actions | `results/k8s/sp4-validation-20260708-023552/web_console` | [ / { / "action": "status_json", |

## 生成时 Git 工作区状态

- ` M .github/workflows/ci.yml`
- ` M README.md`
- ` M agent/include/eulerpilot.hpp`
- ` M agent/include/psi_gate.hpp`
- ` M agent/skills/cgroup_control/README.md`
- ` M agent/src/builtin_skills/common.hpp`
- ` M agent/src/builtin_skills/network_policy.cpp`
- ` M agent/src/builtin_skills/policy_engine.cpp`
- ` M agent/src/builtin_skills/resource_control.cpp`
- ` M agent/src/builtin_skills/security_policy.cpp`
- ` M agent/src/executors.cpp`
- ` M agent/src/main.cpp`
- ` M agent/src/psi_gate.cpp`
- ` M agent/src/runtime.cpp`
- ` M bench/mixed/run_mixed_adaptive_closure.sh`
- ` M bench/nginx/run_nginx_sched_ext_compare.sh`
- ` M bench/nginx/run_nginx_sched_ext_smoke.sh`
- ` M bench/psi/run_gate_mode_smoke.sh`
- ` M bench/psi/run_loader_wiring_smoke.sh`
- ` M bench/psi/run_psi_agent_smoke.sh`
- ` M bench/redis/run_redis_sched_ext_compare.sh`
- ` M bench/redis/run_redis_sched_ext_smoke.sh`
- ` M bench/redis/run_static_vs_agent_compare.sh`
- ` M bench/throughput/run_throughput_first_benchmark.sh`
- ` M bpf/security_policy.bpf.c`
- ` M configs/agent-metrics.yaml`
- ` M configs/agent.yaml`
- ` M configs/final_evidence_manifest.json`
- ` M configs/policy_engine_security_network_resource.yaml`
- ` M docs/README.md`
- ` M docs/architecture.md`
- ` M docs/defense_final.md`
- ` M docs/defense_qa.md`
- ` M docs/defense_slides_outline.md`
- ` M docs/defense_summary.md`
- ` M docs/delivery_package_index.md`
- ` M docs/demo_final_runbook.md`
- ` M docs/demo_video_recording_script.md`
- ` M docs/final_delivery_status.md`
- ` M docs/final_evidence_index.md`
- ` M docs/final_quality_gate.md`
- ` M docs/final_report_submission.md`
- ` M docs/final_results_summary.md`
- ` M docs/final_security_audit.md`
- ` M docs/final_submission_guide.md`
- ` M docs/final_submission_packet.md`
- ` M docs/final_submission_readme.md`
- ` M docs/final_talk_script.md`
- ` M docs/one_page_summary.md`
- ` M docs/progress_status.md`
- ` M docs/project_brief.md`
- ` M docs/release_process.md`
- ` M docs/resource_control_skill.md`
- ` M docs/sp4_validation_plan.md`
- ` M docs/stage_g_benchmark_freeze.md`
- ` M docs/submission_checklist.md`
- ` M reports/events/network_policy.jsonl`
- ` M results/final/agent-overhead-20260720-170415/agent_overhead_summary.csv`
- ` M results/final/agent-overhead-20260720-170415/agent_overhead_summary_avg.csv`
- ` M results/final/mixed-adaptive-20260720-170840/mixed_adaptive_summary.csv`
- ` M results/final/mixed-adaptive-20260720-170840/mixed_adaptive_timeline.csv`
- ` M results/final/redis-static-vs-agent-20260720-150342/compare_summary_avg.csv`
- ` M results/final/throughput-first-20260720-165544/throughput_summary.csv`
- ` M results/final/throughput-first-20260720-165544/throughput_summary_avg.csv`
- ` M sched/README.md`
- ` M sched/scx_eulerpilot.bpf.c`
- ` M sched/scx_eulerpilot.c`
- ` M scripts/README.md`
- ` M scripts/build_scx_eulerpilot.sh`
- ` M scripts/collect_final_evidence.py`
- ` M scripts/collect_scx_stats.py`
- ` M scripts/create_release_bundle.py`
- ` M scripts/final_quality_gate.sh`
- ` M scripts/rollback.sh`
- ` M scripts/setup_cgroup_v2.sh`
- ` M scripts/sp4_host_gate.sh`
- ` M scripts/verify_release_bundle.py`
- ` M submission/build_and_run.md`
- ` M submission/evidence_summary.md`
- ` M submission/submission_manifest.md`
- ` M tests/integration/test_config_validation.sh`
- ` M web_console/backend/src/server.js`
- ` M web_console/backend/test/jobs.test.js`
- ` M web_console/frontend/src/App.tsx`
- ` M web_console/frontend/src/api.ts`
- ` M web_console/frontend/src/styles.css`
- `?? docs/assets/user-manual/`
- `?? docs/demo_video_5min_script.md`
- `?? "docs/\347\224\250\346\210\267\346\211\213\345\206\214.md"`
- `?? "docs/\347\255\224\350\276\251\346\217\220\344\272\244\346\235\220\346\226\231/"`
- `?? evidence/`
- `?? reports/final_quality_gate_20260726-v6-sp3-final.log`
- `?? reports/final_quality_gate_20260726-v6-sp4-final.log`
- `?? reports/gates/`
- `?? reports/v6_candidate_file_manifest_20260726.md`
- `?? reports/v6_candidate_worktree_status_20260726.md`
- `?? reports/v6_dirty_worktree_intake_20260726.md`
- `?? reports/v6_formal_experiment_suite_20260726.md`
- `?? reports/v6_preflight_status_20260726.md`
- `?? results/network_policy/integration-20260727-094323/`
- `?? results/network_policy/qos-tc-20260727-094323/`
- `?? results/network_policy/xdp-20260727-094323/`
- `?? results/policy_engine/security-network-resource-20260727-095653/`
- `?? results/policy_engine/security-network-resource-20260727-100056/`
- `?? results/policy_engine/security-network-resource-20260727-112124/`
- `?? results/policy_engine/security-network-resource-20260727-112713/`
- `?? results/reports/nginx-scx-smoke-20260727-100645/`
- `?? results/reports/redis-scx-smoke-20260727-100530/`
- `?? results/resource_control/integration-20260727-093745/`
- `?? results/resource_control/io-20260727-093757/`
- `?? results/security_policy/anomaly-rules-20260727-095403/`
- `?? results/security_policy/credential-anomaly-20260727-095416/`
- `?? results/security_policy/integration-20260727-095106/`
- `?? scripts/build_formal_artifact.py`
- `?? scripts/formal_artifact_gate.py`
- `?? scripts/prepare_v6_candidate_worktree.sh`
- `?? scripts/v6_preflight_readiness.sh`
- `?? tests/integration/test_benchmark_release_semantics.sh`
- `?? tests/integration/test_evidence_validation_fixtures.sh`
- `?? tests/integration/test_network_policy_cgroup_ownership.sh`
- `?? tests/integration/test_policy_engine_transaction_model.sh`
- `?? tests/integration/test_resource_control_rollback_model.sh`
- `?? tests/integration/test_scx_loader_ownership.sh`
- `?? tests/integration/test_security_policy_fail_closed.sh`

## 使用方式

```bash
python3 scripts/collect_final_evidence.py
python3 scripts/collect_final_evidence.py --strict
python3 scripts/collect_final_evidence.py --validate-run <result-dir>
python3 scripts/collect_final_evidence.py --validate-suite <suite-dir>
python3 scripts/collect_final_evidence.py --validate-release
```

`--validate-run` 用于单轮 smoke，`--validate-suite` 用于正式实验组，`--validate-release` 等价于最终 strict release gate。
