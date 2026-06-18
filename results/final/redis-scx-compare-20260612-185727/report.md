# Redis sched_ext 后端正式对照报告

## 运行信息

- 结果目录：`/root/EulerPilot/results/final/redis-scx-compare-20260612-185727`
- 主机：`cernet2.net`
- 内核：`6.6.0-olk66-scx`
- 轮数：`3`
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
| GET | 32138.143 | 0.423 | 33504.493 | 0.340 | 33825.370 | 0.439 | 34500.467 | 0.423 | 32060.277 | 0.455 | 35182.250 | 0.418 | 33716.720 | 0.418 |
| INCR | 32025.310 | 0.396 | 34661.510 | 0.343 | 33699.800 | 0.383 | 32525.627 | 0.423 | 33890.593 | 0.431 | 36462.643 | 0.404 | 31622.650 | 0.495 |
| PING_INLINE | 32988.307 | 0.351 | 34299.337 | 0.354 | 34094.450 | 0.434 | 31408.093 | 0.447 | 34145.773 | 0.378 | 36121.330 | 0.380 | 32968.543 | 0.495 |
| SET | 32430.657 | 0.383 | 33137.727 | 0.359 | 34226.567 | 0.394 | 33655.677 | 0.412 | 36324.577 | 0.372 | 34817.383 | 0.370 | 35605.320 | 0.420 |

## 相对 `noisy_default` 的自动观察

### GET
- `quiet_default`：RPS -4.99%，P99 -3.64%
- `quiet_scx_normal`：RPS -0.95%，P99 -22.55%
- `noisy_cgroup_v2`：RPS 2.00%，P99 -3.64%
- `noisy_scx_normal`：RPS -5.22%，P99 3.64%
- `noisy_scx_always_active`：RPS 4.01%，P99 -4.78%
- `noisy_scx_psi`：RPS -0.32%，P99 -4.78%
### INCR
- `quiet_default`：RPS -4.97%，P99 3.39%
- `quiet_scx_normal`：RPS 2.85%，P99 -10.44%
- `noisy_cgroup_v2`：RPS -3.48%，P99 10.44%
- `noisy_scx_normal`：RPS 0.57%，P99 12.53%
- `noisy_scx_always_active`：RPS 8.20%，P99 5.48%
- `noisy_scx_psi`：RPS -6.16%，P99 29.24%
### PING_INLINE
- `quiet_default`：RPS -3.24%，P99 -19.12%
- `quiet_scx_normal`：RPS 0.60%，P99 -18.43%
- `noisy_cgroup_v2`：RPS -7.88%，P99 3.00%
- `noisy_scx_normal`：RPS 0.15%，P99 -12.90%
- `noisy_scx_always_active`：RPS 5.94%，P99 -12.44%
- `noisy_scx_psi`：RPS -3.30%，P99 14.06%
### SET
- `quiet_default`：RPS -5.25%，P99 -2.79%
- `quiet_scx_normal`：RPS -3.18%，P99 -8.88%
- `noisy_cgroup_v2`：RPS -1.67%，P99 4.57%
- `noisy_scx_normal`：RPS 6.13%，P99 -5.58%
- `noisy_scx_always_active`：RPS 1.73%，P99 -6.09%
- `noisy_scx_psi`：RPS 4.03%，P99 6.60%

## 运行边界

- `redis-server` 为被保护服务，`stress-ng` 为后台干扰任务。
- `redis-benchmark` 固定留在默认根组，不参与分类控制。
- `cgroup_v2` 组运行前必须保证 `sched_ext state=disabled`。
- `sched_ext` 组运行时必须保证 `sched_ext state=enabled`，并保留 gate 状态与统计快照。

## 当前结论

- 本报告用于正式后端对照阶段的第一轮汇总，后续还需要继续扩大 `RUNS` 并补图表。
