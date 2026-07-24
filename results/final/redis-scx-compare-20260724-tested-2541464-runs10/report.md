# Redis sched_ext 后端正式对照报告

## 运行信息

- 结果目录：`/root/eulerpilot-runs/2541464552aa763522a8496a5082a514a843a179/formal-20260723-153923/redis-scx-compare-runs10`
- 主机：`cernet2.net`
- 内核：`6.6.0-159.4.3.154.oe2403sp4.x86_64-eulerpilot-scx`
- 轮数：`10`
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

| 测试项 | quiet_default RPS | quiet_default P99(ms) | quiet_default CPU/10k | quiet_scx_normal RPS | quiet_scx_normal P99(ms) | quiet_scx_normal CPU/10k | noisy_default RPS | noisy_default P99(ms) | noisy_default CPU/10k | noisy_cgroup_v2 RPS | noisy_cgroup_v2 P99(ms) | noisy_cgroup_v2 CPU/10k | noisy_scx_normal RPS | noisy_scx_normal P99(ms) | noisy_scx_normal CPU/10k | noisy_scx_always_active RPS | noisy_scx_always_active P99(ms) | noisy_scx_always_active CPU/10k | noisy_scx_psi RPS | noisy_scx_psi P99(ms) | noisy_scx_psi CPU/10k |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| GET | 35982.827 | 0.417 | 423.600 | 36133.404 | 0.412 | 424.637 | 17817.630 | 3.026 | 659.138 | 27995.558 | 2.359 | 600.675 | 5749.438 | 3.648 | 626.550 | 7711.932 | 5.834 | 687.812 | 9613.600 | 4.949 | 1567.700 |
| INCR | 35627.228 | 0.459 | 423.600 | 36157.898 | 0.396 | 424.637 | 18594.318 | 2.891 | 659.138 | 31557.250 | 1.813 | 600.675 | 17690.308 | 3.080 | 626.550 | 12322.467 | 4.922 | 687.812 | 9853.862 | 4.782 | 1567.700 |
| PING_INLINE | 30873.065 | 0.908 | 423.600 | 31059.417 | 0.835 | 424.637 | 17499.015 | 3.573 | 659.138 | 17712.540 | 3.528 | 600.675 | 17449.244 | 3.585 | 626.550 | 17755.059 | 3.037 | 687.812 | 6869.609 | 8.608 | 1567.700 |
| SET | 35906.915 | 0.417 | 423.600 | 8130.870 | 0.913 | 424.637 | 17506.539 | 2.970 | 659.138 | 19383.188 | 2.821 | 600.675 | 17415.510 | 2.982 | 626.550 | 17367.054 | 3.157 | 687.812 | 10005.132 | 4.896 | 1567.700 |

## 相对 `noisy_default` 的自动观察

### GET
- `quiet_default`：RPS 101.95%，P99 -86.22%
- `quiet_scx_normal`：RPS 102.80%，P99 -86.38%
- `noisy_cgroup_v2`：RPS 57.12%，P99 -22.04%
- `noisy_scx_normal`：RPS -67.73%，P99 20.56%
- `noisy_scx_always_active`：RPS -56.72%，P99 92.80%
- `noisy_scx_psi`：RPS -46.04%，P99 63.55%
### INCR
- `quiet_default`：RPS 91.60%，P99 -84.12%
- `quiet_scx_normal`：RPS 94.46%，P99 -86.30%
- `noisy_cgroup_v2`：RPS 69.71%，P99 -37.29%
- `noisy_scx_normal`：RPS -4.86%，P99 6.54%
- `noisy_scx_always_active`：RPS -33.73%，P99 70.25%
- `noisy_scx_psi`：RPS -47.01%，P99 65.41%
### PING_INLINE
- `quiet_default`：RPS 76.43%，P99 -74.59%
- `quiet_scx_normal`：RPS 77.49%，P99 -76.63%
- `noisy_cgroup_v2`：RPS 1.22%，P99 -1.26%
- `noisy_scx_normal`：RPS -0.28%，P99 0.34%
- `noisy_scx_always_active`：RPS 1.46%，P99 -15.00%
- `noisy_scx_psi`：RPS -60.74%，P99 140.92%
### SET
- `quiet_default`：RPS 105.11%，P99 -85.96%
- `quiet_scx_normal`：RPS -53.56%，P99 -69.26%
- `noisy_cgroup_v2`：RPS 10.72%，P99 -5.02%
- `noisy_scx_normal`：RPS -0.52%，P99 0.40%
- `noisy_scx_always_active`：RPS -0.80%，P99 6.30%
- `noisy_scx_psi`：RPS -42.85%，P99 64.85%

## 运行边界

- `redis-server` 为被保护服务，`stress-ng` 为后台干扰任务。
- `redis-benchmark` 固定留在默认根组，不参与分类控制。
- `cgroup_v2` 组运行前必须保证 `sched_ext state=disabled`。
- `sched_ext` 组运行时必须保证 `sched_ext state=enabled`，并保留 gate 状态与统计快照。

## 当前结论

- 本报告用于正式后端对照阶段的第一轮汇总，后续还需要继续扩大 `RUNS` 并补图表。
