# EulerPilot v6 Formal Experiment Suite Summary

更新时间：`2026-07-26`

## 绑定信息

- `tested_code_commit`: `7a99d87048f4f2040377354bfe0ce21401664642`
- `artifact_id`: `ef1baebec7ac138acd0eb1a59fc3880ca550330ef89f87615e8579a0ef264240`
- `formal_experiment_root`: `/root/eulerpilot-runs/7a99d87048f4f2040377354bfe0ce21401664642/formal-experiments/20260726-162050`
- `build_manifest`: `/root/eulerpilot-artifacts/7a99d87048f4f2040377354bfe0ce21401664642/ef1baebec7ac138acd0eb1a59fc3880ca550330ef89f87615e8579a0ef264240/manifests/build_manifest.json`
- 输出位置：仓库外 `/root/eulerpilot-runs/...`，原始 JSON/CSV/trace/log 不在本提交中改写。

## Suite Validation

| suite | status | summary | warnings |
| --- | --- | --- | --- |
| redis | present | csv_rows=4; test=GET; quiet_default_rps_avg=36005.720; quiet_scx_normal_rps_avg=26687.541; noisy_default_rps_avg=18632.174; noisy_cgroup_v2_rps_avg=31154.868; noisy_scx_psi_rps_avg=7919.747 | - |
| nginx | present | csv_rows=1; quiet_default_rps_avg=21169.683; quiet_scx_normal_rps_avg=19879.530; noisy_default_rps_avg=14279.439; noisy_cgroup_v2_rps_avg=15280.752; noisy_scx_psi_rps_avg=10406.060 | - |
| throughput | present | throughput_first_validity=pass; throughput_first_runs=10 | - |
| mixed | present | mixed_adaptive_validity=pass; mixed_adaptive_runs=10 | - |
| overhead | present | agent_overhead_validity=pass; agent_overhead_runs=10 | - |

## 性能与功能结论口径

- cgroup v2：在修正 default noisy baseline 后，Redis GET/INCR 有明确正向收益；Nginx 在本次 RUNS=10 中也呈现 RPS 与 P99 改善。
- sched_ext/scx：formal artifact 已证明控制链、batch dispatch 记账和 mixed-adaptive 状态闭环可运行；性能结论必须按 workload 拆分，不能宣传为稳定全面提升。
- SCX PSI/always-active：多数组合存在明显回退，应在答辩中作为调优边界如实说明。
- Mixed-Adaptive：定位为同一 Agent 实例内的 `NORMAL -> ARMED -> ACTIVE -> COOLDOWN -> NORMAL` 功能闭环验证，不作为净性能提升结论。
- Agent overhead：用户态 Agent 三种模式平均约 0.45% 到 0.55% 单核，RSS 平均约 7.4 到 7.5 MB。

## Formal Effect Summary

# Formal Effect Summary

- source_root: `/root/eulerpilot-runs/7a99d87048f4f2040377354bfe0ce21401664642/formal-experiments/20260726-162050`
- baseline: `noisy_default` with corrected default cgroup weights (`cpu.weight=100`, `cpu.max=max`)
- CI: paired per-run mean +/- t(0.975, n-1) * sd/sqrt(n). Positive P99 means lower latency.

## Redis

### GET

| comparison | n | RPS change % mean [95% CI] | P99 improvement % mean [95% CI] |
|---|---:|---:|---:|
| noisy_cgroup_v2 | 10 | 68.30 [50.74, 85.86] | 27.23 [-28.22, 82.67] |
| noisy_scx_normal | 10 | 3.40 [-3.28, 10.08] | -37.75 [-138.61, 63.11] |
| noisy_scx_always_active | 10 | -62.62 [-81.57, -43.67] | -220.20 [-392.92, -47.48] |
| noisy_scx_psi | 10 | -57.60 [-65.15, -50.06] | -105.98 [-170.69, -41.26] |

### INCR

| comparison | n | RPS change % mean [95% CI] | P99 improvement % mean [95% CI] |
|---|---:|---:|---:|
| noisy_cgroup_v2 | 10 | 83.09 [65.21, 100.98] | 52.27 [26.41, 78.13] |
| noisy_scx_normal | 10 | 17.01 [8.41, 25.61] | 48.54 [11.58, 85.50] |
| noisy_scx_always_active | 10 | 4.19 [-2.83, 11.21] | -24.60 [-107.88, 58.69] |
| noisy_scx_psi | 10 | -55.80 [-61.68, -49.93] | -76.83 [-131.12, -22.54] |

### PING_INLINE

| comparison | n | RPS change % mean [95% CI] | P99 improvement % mean [95% CI] |
|---|---:|---:|---:|
| noisy_cgroup_v2 | 10 | 2.12 [-4.34, 8.59] | -3.56 [-19.60, 12.48] |
| noisy_scx_normal | 10 | -0.41 [-5.96, 5.15] | -6.16 [-20.16, 7.84] |
| noisy_scx_always_active | 10 | 0.35 [-3.21, 3.91] | -7.36 [-16.74, 2.02] |
| noisy_scx_psi | 10 | -69.46 [-76.19, -62.73] | -551.27 [-635.58, -466.96] |

### SET

| comparison | n | RPS change % mean [95% CI] | P99 improvement % mean [95% CI] |
|---|---:|---:|---:|
| noisy_cgroup_v2 | 10 | -0.49 [-3.30, 2.31] | -0.38 [-14.79, 14.03] |
| noisy_scx_normal | 10 | -6.21 [-11.97, -0.45] | -3.54 [-18.31, 11.24] |
| noisy_scx_always_active | 10 | -2.03 [-5.15, 1.08] | 5.21 [-15.86, 26.28] |
| noisy_scx_psi | 10 | -58.87 [-67.25, -50.48] | -44.74 [-70.66, -18.82] |

## Nginx

| comparison | n | RPS change % mean [95% CI] | P99 improvement % mean [95% CI] |
|---|---:|---:|---:|
| noisy_cgroup_v2 | 10 | 7.07 [4.51, 9.63] | 18.49 [13.87, 23.11] |
| noisy_scx_normal | 10 | -0.13 [-1.81, 1.55] | -11.86 [-18.87, -4.86] |
| noisy_scx_always_active | 10 | -19.76 [-24.75, -14.78] | -68.93 [-99.97, -37.89] |
| noisy_scx_psi | 10 | -27.09 [-28.71, -25.46] | -70.39 [-85.60, -55.17] |


## Throughput-first Summary CSV

```csv
label,runs,ops_per_sec_avg,agent_applied_count_avg,throughput_profile_hits_avg,class_hits_batch_avg,enqueue_batch_avg,dispatch_batch_dsq_avg,dispatch_batch_local_avg,dispatch_batch_shared_fallback_avg,batch_dispatch_total_avg,running_batch_avg,counter_delta_valid_avg,collection_valid_avg,classification_valid_avg,dispatch_accounting_valid_avg,workload_completion_valid_avg,scheduler_stability_valid_avg,completion_actual_avg
default_batch,10,203673856.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,1.000,1.000,0.000,1.000,1.000,1.000,1629390848.000
cgroup_throughput_first,10,203467079.680,32.000,32.000,0.000,0.000,0.000,0.000,0.000,0.000,0.000,1.000,1.000,0.000,1.000,1.000,1.000,2034670796.800
scx_throughput_first,10,201770352.640,32.000,2.000,485.900,485.900,485.400,0.000,0.000,485.400,497.400,1.000,1.000,1.000,1.000,1.000,1.000,2017703526.400
```

## Mixed-Adaptive Summary CSV

```csv
phase,runs,get_rps_avg,get_p99_ms_avg,active_seen_count,cooldown_seen_count,recovery_seen_count,switch_latency_ms_avg,scheduler_update_evidence_count
quiet_pre,10,27823.438,0.881,0,0,0,,10
pressure_active,10,26386.807,0.978,10,0,0,1712.821,10
recovery,10,26368.637,0.877,10,10,10,0.000,10
```

## Agent Overhead Summary CSV

```csv
label,runs_present,cpu_seconds_avg,cpu_percent_of_one_core_avg,rss_kb_avg,rss_kb_max,skipped
observe_only_cgroup,10,0.036,0.450,7494.053,15544,0
active_cgroup,10,0.041,0.512,7394.653,15248,0
active_sched_ext,10,0.044,0.550,7548.533,13448,0
```
