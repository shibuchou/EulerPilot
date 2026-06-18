# Redis sched_ext 后端正式对照报告

## 运行信息

- 结果目录：`/root/EulerPilot/results/final/redis-scx-compare-20260612-184315`
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
| GET | 33222.590 | 0.399 | 31948.880 | 0.319 | 33670.040 | 0.431 | 31446.540 | 0.487 | 33222.590 | 0.239 | 34843.210 | 0.447 | 30120.480 | 0.495 |
| INCR | 31746.030 | 0.391 | 30674.850 | 0.399 | 31250.000 | 0.431 | 31847.130 | 0.479 | 32258.060 | 0.255 | 36363.640 | 0.431 | 33444.820 | 0.447 |
| PING_INLINE | 31746.030 | 0.383 | 29940.120 | 0.359 | 31948.880 | 0.391 | 32894.740 | 0.439 | 31055.900 | 0.407 | 31746.030 | 0.487 | 33783.790 | 0.359 |
| SET | 33557.050 | 0.375 | 33222.590 | 0.343 | 36496.350 | 0.375 | 29498.530 | 0.423 | 33222.590 | 0.279 | 34965.040 | 0.431 | 33112.590 | 0.431 |

## 相对 `noisy_default` 的自动观察

### GET
- `quiet_default`：RPS -1.33%，P99 -7.42%
- `quiet_scx_normal`：RPS -5.11%，P99 -25.99%
- `noisy_cgroup_v2`：RPS -6.60%，P99 12.99%
- `noisy_scx_normal`：RPS -1.33%，P99 -44.55%
- `noisy_scx_always_active`：RPS 3.48%，P99 3.71%
- `noisy_scx_psi`：RPS -10.54%，P99 14.85%
### INCR
- `quiet_default`：RPS 1.59%，P99 -9.28%
- `quiet_scx_normal`：RPS -1.84%，P99 -7.42%
- `noisy_cgroup_v2`：RPS 1.91%，P99 11.14%
- `noisy_scx_normal`：RPS 3.23%，P99 -40.84%
- `noisy_scx_always_active`：RPS 16.36%，P99 0.00%
- `noisy_scx_psi`：RPS 7.02%，P99 3.71%
### PING_INLINE
- `quiet_default`：RPS -0.63%，P99 -2.05%
- `quiet_scx_normal`：RPS -6.29%，P99 -8.18%
- `noisy_cgroup_v2`：RPS 2.96%，P99 12.28%
- `noisy_scx_normal`：RPS -2.80%，P99 4.09%
- `noisy_scx_always_active`：RPS -0.63%，P99 24.55%
- `noisy_scx_psi`：RPS 5.74%，P99 -8.18%
### SET
- `quiet_default`：RPS -8.05%，P99 0.00%
- `quiet_scx_normal`：RPS -8.97%，P99 -8.53%
- `noisy_cgroup_v2`：RPS -19.17%，P99 12.80%
- `noisy_scx_normal`：RPS -8.97%，P99 -25.60%
- `noisy_scx_always_active`：RPS -4.20%，P99 14.93%
- `noisy_scx_psi`：RPS -9.27%，P99 14.93%

## 运行边界

- `redis-server` 为被保护服务，`stress-ng` 为后台干扰任务。
- `redis-benchmark` 固定留在默认根组，不参与分类控制。
- `cgroup_v2` 组运行前必须保证 `sched_ext state=disabled`。
- `sched_ext` 组运行时必须保证 `sched_ext state=enabled`，并保留 gate 状态与统计快照。

## 当前结论

- 本报告用于正式后端对照阶段的第一轮汇总，后续还需要继续扩大 `RUNS` 并补图表。
