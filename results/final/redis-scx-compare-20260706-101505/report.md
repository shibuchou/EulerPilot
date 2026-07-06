# Redis sched_ext 后端正式对照报告

## 运行信息

- 结果目录：`/root/EulerPilot/results/final/redis-scx-compare-20260706-101505`
- 主机：`cernet2.net`
- 内核：`6.6.0-159.4.3.154.oe2403sp4.x86_64-eulerpilot-scx`
- 轮数：`1`
- Redis 端口：`6386`
- bench clients：`16`
- bench requests：`20000`
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
| GET | 33670.040 | 0.663 | 35778.180 | 0.407 | 15822.780 | 3.135 | 31645.570 | 1.775 | 18231.540 | 2.831 | 18034.270 | 2.839 | 10075.570 | 5.383 |
| INCR | 35714.290 | 0.407 | 36166.370 | 0.391 | 14760.150 | 3.175 | 29069.770 | 2.319 | 17985.610 | 3.087 | 18315.020 | 2.879 | 11883.540 | 4.823 |
| PING_INLINE | 24242.420 | 0.751 | 27173.910 | 1.031 | 18832.390 | 2.111 | 11661.810 | 3.767 | 2698.690 | 4.047 | 4767.580 | 5.927 | 2418.960 | 6.335 |
| SET | 35398.230 | 0.407 | 35842.290 | 0.391 | 19801.980 | 0.895 | 35523.980 | 0.431 | 17667.850 | 3.015 | 17182.130 | 2.895 | 9425.070 | 5.303 |

## 相对 `noisy_default` 的自动观察

### GET
- `quiet_default`：RPS 112.79%，P99 -78.85%
- `quiet_scx_normal`：RPS 126.12%，P99 -87.02%
- `noisy_cgroup_v2`：RPS 100.00%，P99 -43.38%
- `noisy_scx_normal`：RPS 15.22%，P99 -9.70%
- `noisy_scx_always_active`：RPS 13.98%，P99 -9.44%
- `noisy_scx_psi`：RPS -36.32%，P99 71.71%
### INCR
- `quiet_default`：RPS 141.96%，P99 -87.18%
- `quiet_scx_normal`：RPS 145.03%，P99 -87.69%
- `noisy_cgroup_v2`：RPS 96.95%，P99 -26.96%
- `noisy_scx_normal`：RPS 21.85%，P99 -2.77%
- `noisy_scx_always_active`：RPS 24.08%，P99 -9.32%
- `noisy_scx_psi`：RPS -19.49%，P99 51.91%
### PING_INLINE
- `quiet_default`：RPS 28.73%，P99 -64.42%
- `quiet_scx_normal`：RPS 44.29%，P99 -51.16%
- `noisy_cgroup_v2`：RPS -38.08%，P99 78.45%
- `noisy_scx_normal`：RPS -85.67%，P99 91.71%
- `noisy_scx_always_active`：RPS -74.68%，P99 180.77%
- `noisy_scx_psi`：RPS -87.16%，P99 200.09%
### SET
- `quiet_default`：RPS 78.76%，P99 -54.53%
- `quiet_scx_normal`：RPS 81.00%，P99 -56.31%
- `noisy_cgroup_v2`：RPS 79.40%，P99 -51.84%
- `noisy_scx_normal`：RPS -10.78%，P99 236.87%
- `noisy_scx_always_active`：RPS -13.23%，P99 223.46%
- `noisy_scx_psi`：RPS -52.40%，P99 492.51%

## 运行边界

- `redis-server` 为被保护服务，`stress-ng` 为后台干扰任务。
- `redis-benchmark` 固定留在默认根组，不参与分类控制。
- `cgroup_v2` 组运行前必须保证 `sched_ext state=disabled`。
- `sched_ext` 组运行时必须保证 `sched_ext state=enabled`，并保留 gate 状态与统计快照。

## 当前结论

- 本报告用于正式后端对照阶段的第一轮汇总，后续还需要继续扩大 `RUNS` 并补图表。
