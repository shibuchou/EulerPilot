# EulerPilot formal experiment batch 2 summary

- tested_code_commit: `2541464552aa763522a8496a5082a514a843a179`
- output_root: `/root/eulerpilot-runs/2541464552aa763522a8496a5082a514a843a179/formal-20260723-153923`
- source_tree_clean_at_end: `True`

## Completed result directories
- `redis_scx_compare_runs10`: `redis-scx-compare-runs10`, status=`complete`, invalid_markers=`0`
- `nginx_scx_compare_runs10`: `nginx-scx-compare-runs10`, status=`complete`, invalid_markers=`0`
- `redis_static_vs_agent_runs10`: `redis-static-vs-agent-runs10`, status=`complete`, invalid_markers=`0`
- `agent_overhead_runs10`: `agent-overhead-runs10`, status=`complete`, invalid_markers=`0`
- `throughput_first_runs10`: `throughput-first-runs10`, status=`complete`, invalid_markers=`0`
- `mixed_adaptive_runs10_original`: `mixed-adaptive-runs10`, status=`invalid_retained`, invalid_markers=`1`
- `mixed_adaptive_runs10_lite`: `mixed-adaptive-runs10-lite`, status=`complete`, invalid_markers=`0`
- `redis_pressure_gradient_runs3`: `redis-pressure-gradient-runs3`, status=`complete`, invalid_markers=`0`
## Key conclusions
- Redis noisy cgroup v2 remains the strongest performance evidence; pressure gradient shows the benefit grows under heavier interference for GET.
- Nginx and sched_ext/scx are workload boundary evidence; they validate compatibility and control paths, but must be reported as neutral/regressed where the data regresses.
- `noisy_scx_psi` includes extra PSI probe load in relevant experiments and must not be used as net performance improvement proof.
- Mixed-adaptive lite provides valid closed-loop evidence for `NORMAL -> ARMED -> ACTIVE -> COOLDOWN -> NORMAL`; the original mixed run is retained as invalid parameter-boundary evidence.
- CPU/10k requests is auxiliary/exploratory because it is derived from system-wide `/proc/stat`; primary conclusions should use RPS, latency percentiles, PSI, workload completion, and SCX stats semantics.

## Redis pressure gradient GET table
| workers | noisy_default_rps | noisy_cgroup_v2_rps | noisy_scx_psi_rps | noisy_default_p99_ms | noisy_cgroup_v2_p99_ms | noisy_scx_psi_p99_ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | 36171.210 | 36342.037 | 8531.840 | 0.402 | 0.423 | 3.452 |
| 1 | 20556.080 | 26015.433 | 9442.210 | 0.839 | 0.866 | 4.439 |
| 2 | 16599.850 | 31157.923 | 10404.600 | 3.260 | 2.276 | 5.071 |
| 4 | 12319.077 | 29105.007 | 9143.727 | 4.850 | 1.871 | 5.492 |
| 8 | 9292.700 | 33291.173 | 8503.623 | 5.439 | 1.295 | 5.554 |
