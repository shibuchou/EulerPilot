# Redis sched_ext 后端正式对照报告

## 运行信息

- 结果目录：`/root/EulerPilot/results/final/redis-scx-compare-20260612-185007`
- 主机：`cernet2.net`
- 内核：`6.6.0-olk66-scx`
- 轮数：`1`
- Redis 端口：`6386`
- bench clients：`8`
- bench requests：`10000`
- stress workers：`2`
- sched_ext switch mode：`full`

## 组别说明

- `quiet_default`：仅 Redis，默认调度器
- `quiet_scx_normal`：仅 Redis，sched_ext 常驻但保持 normal
- `noisy_default`：Redis + stress-ng，默认调度器
- `noisy_cgroup_v2`：Redis + stress-ng，cgroup v2 控制
- `noisy_scx_normal`：Redis + stress-ng，sched_ext normal
- `noisy_scx_always_active`：Redis + stress-ng，sched_ext always-active
- `noisy_scx_psi`：Redis + stress-ng，sched_ext psi

## 汇总表

| 测试项 | quiet_default RPS | quiet_default P99(ms) | quiet_scx_normal RPS | quiet_scx_normal P99(ms) | noisy_default RPS | noisy_default P99(ms) | noisy_cgroup_v2 RPS | noisy_cgroup_v2 P99(ms) | noisy_scx_normal RPS | noisy_scx_normal P99(ms) | noisy_scx_always_active RPS | noisy_scx_always_active P99(ms) | noisy_scx_psi RPS | noisy_scx_psi P99(ms) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| GET | 31545.740 | 0.391 | 32573.290 | 0.399 | 36101.080 | 0.463 | 29940.120 | 0.703 | 33670.040 | 0.423 | 32467.530 | 0.431 | 34602.070 | 0.447 |
| INCR | 33003.300 | 0.327 | 30487.800 | 0.383 | 35087.720 | 0.471 | 37878.790 | 0.303 | 28328.610 | 0.479 | 33898.300 | 0.319 | 44052.860 | 0.327 |
| PING_INLINE | 30959.750 | 0.399 | 30395.140 | 0.407 | 34246.570 | 0.335 | 34722.220 | 0.327 | 32258.060 | 0.367 | 33444.820 | 0.431 | 33898.300 | 0.423 |
| SET | 31645.570 | 0.407 | 35971.220 | 0.319 | 31347.960 | 0.503 | 38314.180 | 0.399 | 35714.290 | 0.295 | 30211.480 | 0.471 | 39062.500 | 0.423 |

## 相对 `noisy_default` 的自动观察

### GET
- `quiet_default`：RPS -12.62%，P99 -15.55%
- `quiet_scx_normal`：RPS -9.77%，P99 -13.82%
- `noisy_cgroup_v2`：RPS -17.07%，P99 51.84%
- `noisy_scx_normal`：RPS -6.73%，P99 -8.64%
- `noisy_scx_always_active`：RPS -10.06%，P99 -6.91%
- `noisy_scx_psi`：RPS -4.15%，P99 -3.46%
### INCR
- `quiet_default`：RPS -5.94%，P99 -30.57%
- `quiet_scx_normal`：RPS -13.11%，P99 -18.68%
- `noisy_cgroup_v2`：RPS 7.95%，P99 -35.67%
- `noisy_scx_normal`：RPS -19.26%，P99 1.70%
- `noisy_scx_always_active`：RPS -3.39%，P99 -32.27%
- `noisy_scx_psi`：RPS 25.55%，P99 -30.57%
### PING_INLINE
- `quiet_default`：RPS -9.60%，P99 19.10%
- `quiet_scx_normal`：RPS -11.25%，P99 21.49%
- `noisy_cgroup_v2`：RPS 1.39%，P99 -2.39%
- `noisy_scx_normal`：RPS -5.81%，P99 9.55%
- `noisy_scx_always_active`：RPS -2.34%，P99 28.66%
- `noisy_scx_psi`：RPS -1.02%，P99 26.27%
### SET
- `quiet_default`：RPS 0.95%，P99 -19.09%
- `quiet_scx_normal`：RPS 14.75%，P99 -36.58%
- `noisy_cgroup_v2`：RPS 22.22%，P99 -20.68%
- `noisy_scx_normal`：RPS 13.93%，P99 -41.35%
- `noisy_scx_always_active`：RPS -3.63%，P99 -6.36%
- `noisy_scx_psi`：RPS 24.61%，P99 -15.90%

## 运行边界

- `redis-server` 为被保护服务，`stress-ng` 为后台干扰任务。
- `redis-benchmark` 固定留在默认根组，不参与分类控制。
- `cgroup_v2` 组运行前必须保证 `sched_ext state=disabled`。
- `sched_ext` 组运行时必须保证 `sched_ext state=enabled`，并保留 gate 状态与统计快照。

## 当前结论

- 本报告用于正式后端对照阶段的第一轮汇总，后续还需要继续扩大 `RUNS` 并补图表。
