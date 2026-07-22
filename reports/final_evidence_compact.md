# EulerPilot 最终证据压缩报告

生成时间：`2026-07-22T10:39:37+08:00`
清单：`configs/final_evidence_manifest.json`

本报告由 `scripts/collect_final_evidence.py` 根据白名单清单生成，用于把分散的双机结果压缩成答辩入口。它不会递归扫描全部 `results/`，缺失项会显式列出。

## 总览

| 指标 | 值 |
| --- | --- |
| 清单条目 | 40 |
| 必需缺失 | 0 |
| 带警告条目 | 0 |
| Git 工作区额外状态 | 46 |

## quality_gate

| 状态 | 主机 | 名称 | 路径 | 摘要 |
| --- | --- | --- | --- | --- |
| pass | 121 | 121 final quality gate after engineering quality closeout | `reports/final_quality_gate_20260706-quality-121.log` | result=pass |
| pass | 123 | SP4 final quality gate after sched_ext workload validation | `reports/sp4/final_quality_gate_scx_workload_20260706-1214.log` | result=pass |
| pass | 123 | SP4 final quality gate after stage3 performance evidence closeout | `reports/final_quality_gate_20260720-stage3-performance.log` | result=pass |
| present | 123 | SP4 Agent control-plane overhead benchmark | `results/final/agent-overhead-20260720-170415` | agent_overhead_validity=pass |

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
| present | 123 | SP4 Redis manual static vs agent dynamic comparison | `results/final/redis-static-vs-agent-20260720-150342` | static_vs_agent_validity=pass<br>static_vs_agent_groups=default_noisy,agent_observe_only,manual_static,agent_dynamic |
| present | 123 | SP4 Redis PSI gate ACTIVE probe | `results/final/redis-scx-psi-probe-20260706-100857` | ﻿# Redis sched_ext PSI ACTIVE Probe / - timestamp: 2026-07-06T10:09:21+08:00 / - redis port: 6390 |
| present | 123 | SP4 throughput-first batch benchmark | `results/final/throughput-first-20260720-165544` | throughput_first_validity=pass |
| present | 123 | SP4 mixed-adaptive closure benchmark | `results/final/mixed-adaptive-20260720-170840` | mixed_adaptive_validity=pass |

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
- ` M AGENTS.md`
- ` M Makefile`
- ` M docs/design_proposal.md`
- ` M docs/development.md`
- ` M docs/final_submission_guide.md`
- ` M docs/reference_repos.md`
- ` M docs/v3_1_start_status_20260629.md`
- ` M reports/final_figures/nginx_sched_ext_p99.svg`
- ` M reports/final_figures/nginx_sched_ext_rps.svg`
- ` M reports/final_figures/redis_sched_ext_p99.svg`
- ` M reports/final_figures/redis_sched_ext_rps.svg`
- ` M reports/final_repo_consistency_20260706-1504.log`
- ` M reports/final_repo_consistency_20260706-1550.log`
- ` M scripts/render_sched_ext_figures.py`
- ` M third_party/reference/kata-lsm-ebpf/README.md`
- ` D third_party/reference/libbpf-bootstrap/examples/c/Packetloss`
- ` D third_party/reference/libbpf-bootstrap/examples/c/Transmission`
- ` D third_party/reference/libbpf-bootstrap/examples/c/buddystat`
- ` D third_party/reference/libbpf-bootstrap/examples/c/cpu_usage`
- ` D third_party/reference/libbpf-bootstrap/examples/c/fileinfo`
- ` D third_party/reference/libbpf-bootstrap/examples/c/iolatency`
- ` D third_party/reference/libbpf-bootstrap/examples/c/latency_5`
- ` D third_party/reference/libbpf-bootstrap/examples/c/merge_latency`
- ` D third_party/reference/libbpf-bootstrap/examples/c/merge_occupancy`
- ` D third_party/reference/libbpf-bootstrap/examples/c/myringbuffer`
- ` D third_party/reference/libbpf-bootstrap/examples/c/nf_hook`
- ` D third_party/reference/libbpf-bootstrap/examples/c/pidmm`
- ` D third_party/reference/libbpf-bootstrap/examples/c/pidmm1`
- ` D third_party/reference/libbpf-bootstrap/examples/c/pidmm2`
- ` D third_party/reference/libbpf-bootstrap/examples/c/psi_pf`
- ` D third_party/reference/libbpf-bootstrap/examples/c/psidata`
- ` D third_party/reference/libbpf-bootstrap/examples/c/test`
- ` D third_party/reference/libbpf-bootstrap/examples/c/tpruntime`
- ` D third_party/reference/libbpf-bootstrap/examples/c/tptest`
- ` D third_party/reference/libbpf-bootstrap/examples/c/usage`
- ` D third_party/reference/libbpf-bootstrap/examples/c/usage2`
- ` M third_party/reference/lmp-xdp-lsm/README.md`
- ` M web_console/backend/src/server.js`
- ` M web_console/frontend/src/App.tsx`
- ` M web_console/frontend/src/api.ts`
- `?? CONTRIBUTING.md`
- `?? LICENSE`
- `?? NOTICE`
- `?? SECURITY.md`
- `?? web_console/backend/test/server.test.js`

## 使用方式

```bash
python3 scripts/collect_final_evidence.py
python3 scripts/collect_final_evidence.py --strict
```

`--strict` 会在必需证据缺失或清单条目存在警告时返回非零退出码，适合最终提交前检查。
