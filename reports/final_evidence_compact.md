# EulerPilot 最终证据压缩报告

生成时间：`2026-07-20T14:32:27+08:00`
清单：`configs/final_evidence_manifest.json`

本报告由 `scripts/collect_final_evidence.py` 根据白名单清单生成，用于把分散的双机结果压缩成答辩入口。它不会递归扫描全部 `results/`，缺失项会显式列出。

## 总览

| 指标 | 值 |
| --- | --- |
| 清单条目 | 37 |
| 必需缺失 | 0 |
| 带警告条目 | 0 |
| Git 工作区额外状态 | 69 |

## quality_gate

| 状态 | 主机 | 名称 | 路径 | 摘要 |
| --- | --- | --- | --- | --- |
| pass | 121 | 121 final quality gate after engineering quality closeout | `reports/final_quality_gate_20260706-quality-121.log` | result=pass |
| pass | 123 | SP4 final quality gate after sched_ext workload validation | `reports/sp4/final_quality_gate_scx_workload_20260706-1214.log` | result=pass |
| pass | 123 | SP4 final quality gate after observer/path closeout | `reports/final_quality_gate_20260720-path-closeout.log` | result=pass |

## repo_status

| 状态 | 主机 | 名称 | 路径 | 摘要 |
| --- | --- | --- | --- | --- |
| present | local | v3.1 start repository snapshot | `reports/v3_1_start_repo_status_20260629-1859.log` | - |

## cpu_sched_ext

| 状态 | 主机 | 名称 | 路径 | 摘要 |
| --- | --- | --- | --- | --- |
| present | 121 | Redis sched_ext comparison | `results/final/redis-scx-compare-20260612-191543` | # Redis sched_ext 正式对照 / - timestamp: 2026-06-12T19:28:10+08:00 / - runs: 5 |
| present | 121 | Nginx sched_ext comparison | `results/final/nginx-scx-compare-20260612-194018` | # Nginx sched_ext 正式对照 / - timestamp: 2026-06-12T19:52:03+08:00 / - runs: 5 |
| present | 123 | SP4 Redis sched_ext comparison RUNS=5 | `results/final/redis-scx-compare-20260708-150702` | # Redis sched_ext 正式对照 / - timestamp: 2026-07-08T15:25:52+08:00 / - runs: 5 |
| present | 123 | SP4 Nginx sched_ext comparison RUNS=5 | `results/final/nginx-scx-compare-20260708-152602` | # Nginx sched_ext 正式对照 / - timestamp: 2026-07-08T15:38:01+08:00 / - runs: 5 |
| present | 123 | SP4 Redis pressure gradient comparison | `results/final/redis-pressure-gradient-20260708-153811` | # Redis 压力递增梯度实验 / - 结果目录：`/root/EulerPilot/results/final/redis-pressure-gradient-20260708-153811` / - worker 档位：0 / 1 / 2 / 4 / 8 |
| present | 123 | SP4 Redis manual static vs agent dynamic comparison | `results/final/redis-static-vs-agent-20260720-114909` | # Redis static vs Agent dynamic compare / - timestamp: 2026-07-20T11:56:11+08:00 / - runs: 5 |
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

- ` M README.md`
- ` M agent/include/skill_runtime_context.hpp`
- ` M agent/src/builtin_skills/common.hpp`
- ` M agent/src/builtin_skills/network_policy.cpp`
- ` M agent/src/builtin_skills/network_qos.cpp`
- ` M agent/src/builtin_skills/network_xdp.cpp`
- ` M agent/src/builtin_skills/resource_control.cpp`
- ` M agent/src/builtin_skills/security_policy.cpp`
- ` M agent/src/executors.cpp`
- ` M agent/src/runtime.cpp`
- ` M bench/mixed/run_placeholder_benchmark.sh`
- ` M bench/nginx/run_nginx_main_experiment.sh`
- ` M bench/nginx/run_nginx_sched_ext_compare.sh`
- ` M bench/nginx/run_nginx_sched_ext_smoke.sh`
- ` M bench/nginx/run_nginx_stress_benchmark.sh`
- ` M bench/psi/run_gate_mode_smoke.sh`
- ` M bench/psi/run_loader_wiring_smoke.sh`
- ` M bench/psi/run_network_policy_smoke.sh`
- ` M bench/psi/run_psi_agent_smoke.sh`
- ` M bench/redis/run_background_weight_refine.sh`
- ` M bench/redis/run_profile_sweep.sh`
- ` M bench/redis/run_psi_threshold_sweep.sh`
- ` M bench/redis/run_redis_final_experiment.sh`
- ` M bench/redis/run_redis_main_experiment.sh`
- ` M bench/redis/run_redis_pressure_gradient.sh`
- ` M bench/redis/run_redis_sched_ext_compare.sh`
- ` M bench/redis/run_redis_sched_ext_psi_probe.sh`
- ` M bench/redis/run_redis_sched_ext_smoke.sh`
- ` M bench/redis/run_redis_stress_benchmark.sh`
- ` M bench/redis/run_static_vs_agent_compare.sh`
- ` M bench/redis/run_trigger_sweep.sh`
- ` M bench/redis/run_trigger_sweep_incr_refine.sh`
- ` M bench/redis/run_trigger_sweep_local_refine.sh`
- ` M bpf/workload_observer.bpf.c`
- ` M bpf/workload_observer.h`
- ` M configs/final_evidence_manifest.json`
- ` M configs/policy_engine_security_network_resource.skills.yaml`
- ` M configs/skills.yaml`
- ` M docs/README.md`
- ` M docs/defense_final.md`
- ` M docs/defense_summary.md`
- ` M docs/demo_final_runbook.md`
- ` M docs/experiments.md`
- ` M docs/final_evidence_index.md`
- ` M docs/final_report_submission.md`
- ` M docs/final_talk_script.md`
- ` M docs/one_page_summary.md`
- ` M docs/progress_status.md`
- ` M docs/project_brief.md`
- ` M docs/project_status_overview.md`
- ` M docs/sp4_validation_plan.md`
- ` M docs/stage_g_benchmark_freeze.md`
- ` M docs/submission_checklist.md`
- ` M docs/web_console_design.md`
- ` M scripts/assign_cgroup.sh`
- ` M scripts/capture_agent_snapshot.sh`
- ` M scripts/capture_gate_runtime.sh`
- ` M scripts/rollback.sh`
- ` M scripts/write_run_manifest.py`
- ` M tests/integration/test_policy_engine_security_network_resource.sh`
- ` M tests/integration/test_policy_engine_security_resource.sh`
- ` M tests/integration/test_security_policy.sh`
- ` M tests/integration/test_security_policy_anomaly_combo_scope.sh`
- ` M tests/integration/test_security_policy_anomaly_process_filter.sh`
- ` M tests/integration/test_security_policy_anomaly_rules.sh`
- ` M tests/integration/test_security_policy_credential_anomaly.sh`
- ` M tests/integration/test_security_policy_credential_deep_hooks.sh`
- `?? reports/final_quality_gate_20260720-path-closeout.log`
- `?? results/final/redis-static-vs-agent-20260720-114909/`

## 使用方式

```bash
python3 scripts/collect_final_evidence.py
python3 scripts/collect_final_evidence.py --strict
```

`--strict` 会在必需证据缺失或清单条目存在警告时返回非零退出码，适合最终提交前检查。
