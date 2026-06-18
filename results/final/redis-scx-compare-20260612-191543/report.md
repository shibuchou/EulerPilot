# Redis sched_ext 后端正式对照报告

## 运行信息

- 结果目录：`/root/EulerPilot/results/final/redis-scx-compare-20260612-191543`
- 主机：`cernet2.net`
- 内核：`6.6.0-olk66-scx`
- 轮数：`5`
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
| GET | 31665.698 | 0.407 | 31044.572 | 0.381 | 34279.036 | 0.409 | 37760.060 | 0.394 | 37673.036 | 0.335 | 31989.440 | 0.482 | 36582.858 | 0.415 |
| INCR | 30948.828 | 0.409 | 32531.770 | 0.372 | 34175.656 | 0.476 | 35516.492 | 0.410 | 39030.928 | 0.335 | 33522.644 | 0.417 | 33260.870 | 0.405 |
| PING_INLINE | 31312.146 | 0.381 | 33055.060 | 0.354 | 33302.420 | 0.413 | 33434.982 | 0.396 | 35310.814 | 0.401 | 36208.784 | 0.407 | 33090.080 | 0.357 |
| SET | 31917.696 | 0.361 | 32231.074 | 0.369 | 35003.902 | 0.418 | 35605.082 | 0.372 | 41941.040 | 0.292 | 33002.556 | 0.441 | 33203.678 | 0.418 |

## 相对 `noisy_default` 的自动观察

### GET
- `quiet_default`：RPS -7.62%，P99 -0.49%
- `quiet_scx_normal`：RPS -9.44%，P99 -6.85%
- `noisy_cgroup_v2`：RPS 10.15%，P99 -3.67%
- `noisy_scx_normal`：RPS 9.90%，P99 -18.09%
- `noisy_scx_always_active`：RPS -6.68%，P99 17.85%
- `noisy_scx_psi`：RPS 6.72%，P99 1.47%
### INCR
- `quiet_default`：RPS -9.44%，P99 -14.08%
- `quiet_scx_normal`：RPS -4.81%，P99 -21.85%
- `noisy_cgroup_v2`：RPS 3.92%，P99 -13.87%
- `noisy_scx_normal`：RPS 14.21%，P99 -29.62%
- `noisy_scx_always_active`：RPS -1.91%，P99 -12.39%
- `noisy_scx_psi`：RPS -2.68%，P99 -14.92%
### PING_INLINE
- `quiet_default`：RPS -5.98%，P99 -7.75%
- `quiet_scx_normal`：RPS -0.74%，P99 -14.29%
- `noisy_cgroup_v2`：RPS 0.40%，P99 -4.12%
- `noisy_scx_normal`：RPS 6.03%，P99 -2.91%
- `noisy_scx_always_active`：RPS 8.73%，P99 -1.45%
- `noisy_scx_psi`：RPS -0.64%，P99 -13.56%
### SET
- `quiet_default`：RPS -8.82%，P99 -13.64%
- `quiet_scx_normal`：RPS -7.92%，P99 -11.72%
- `noisy_cgroup_v2`：RPS 1.72%，P99 -11.00%
- `noisy_scx_normal`：RPS 19.82%，P99 -30.14%
- `noisy_scx_always_active`：RPS -5.72%，P99 5.50%
- `noisy_scx_psi`：RPS -5.14%，P99 0.00%

## 运行边界

- `redis-server` 为被保护服务，`stress-ng` 为后台干扰任务。
- `redis-benchmark` 固定留在默认根组，不参与分类控制。
- `cgroup_v2` 组运行前必须保证 `sched_ext state=disabled`。
- `sched_ext` 组运行时必须保证 `sched_ext state=enabled`，并保留 gate 状态与统计快照。

## 当前结论

- 本报告用于正式后端对照阶段的第一轮汇总，后续还需要继续扩大 `RUNS` 并补图表。
