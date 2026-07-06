# Redis sched_ext 后端正式对照报告

## 运行信息

- 结果目录：`/root/EulerPilot/results/final/redis-scx-compare-20260706-115029`
- 主机：`cernet2.net`
- 内核：`6.6.0-159.4.3.154.oe2403sp4.x86_64-eulerpilot-scx`
- 轮数：`3`
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
| GET | 35285.903 | 0.458 | 35800.853 | 0.476 | 16532.470 | 3.300 | 31942.873 | 1.684 | 17890.783 | 3.167 | 17444.407 | 3.130 | 9864.123 | 5.306 |
| INCR | 35521.113 | 0.412 | 35298.350 | 0.508 | 16146.137 | 3.335 | 33352.130 | 1.420 | 17619.137 | 2.954 | 17423.190 | 2.986 | 9970.860 | 4.892 |
| PING_INLINE | 33581.730 | 0.743 | 11551.443 | 1.178 | 15934.550 | 3.839 | 14975.123 | 3.919 | 2679.697 | 4.154 | 5001.510 | 5.887 | 2432.243 | 6.426 |
| SET | 35364.993 | 0.420 | 24927.973 | 0.932 | 16986.340 | 3.223 | 32604.917 | 1.420 | 18052.133 | 2.831 | 18653.867 | 2.498 | 9838.313 | 5.204 |

## 相对 `noisy_default` 的自动观察

### GET
- `quiet_default`：RPS 113.43%，P99 -86.12%
- `quiet_scx_normal`：RPS 116.55%，P99 -85.58%
- `noisy_cgroup_v2`：RPS 93.21%，P99 -48.97%
- `noisy_scx_normal`：RPS 8.22%，P99 -4.03%
- `noisy_scx_always_active`：RPS 5.52%，P99 -5.15%
- `noisy_scx_psi`：RPS -40.33%，P99 60.79%
### INCR
- `quiet_default`：RPS 120.00%，P99 -87.65%
- `quiet_scx_normal`：RPS 118.62%，P99 -84.77%
- `noisy_cgroup_v2`：RPS 106.56%，P99 -57.42%
- `noisy_scx_normal`：RPS 9.12%，P99 -11.42%
- `noisy_scx_always_active`：RPS 7.91%，P99 -10.46%
- `noisy_scx_psi`：RPS -38.25%，P99 46.69%
### PING_INLINE
- `quiet_default`：RPS 110.75%，P99 -80.65%
- `quiet_scx_normal`：RPS -27.51%，P99 -69.31%
- `noisy_cgroup_v2`：RPS -6.02%，P99 2.08%
- `noisy_scx_normal`：RPS -83.18%，P99 8.21%
- `noisy_scx_always_active`：RPS -68.61%，P99 53.35%
- `noisy_scx_psi`：RPS -84.74%，P99 67.39%
### SET
- `quiet_default`：RPS 108.20%，P99 -86.97%
- `quiet_scx_normal`：RPS 46.75%，P99 -71.08%
- `noisy_cgroup_v2`：RPS 91.95%，P99 -55.94%
- `noisy_scx_normal`：RPS 6.27%，P99 -12.16%
- `noisy_scx_always_active`：RPS 9.82%，P99 -22.49%
- `noisy_scx_psi`：RPS -42.08%，P99 61.46%

## 运行边界

- `redis-server` 为被保护服务，`stress-ng` 为后台干扰任务。
- `redis-benchmark` 固定留在默认根组，不参与分类控制。
- `cgroup_v2` 组运行前必须保证 `sched_ext state=disabled`。
- `sched_ext` 组运行时必须保证 `sched_ext state=enabled`，并保留 gate 状态与统计快照。

## 当前结论

- 本报告用于正式后端对照阶段的第一轮汇总，后续还需要继续扩大 `RUNS` 并补图表。
