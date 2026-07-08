# Redis sched_ext 后端正式对照报告

## 运行信息

- 结果目录：`/root/EulerPilot/results/final/redis-scx-compare-20260708-150702`
- 主机：`cernet2.net`
- 内核：`6.6.0-159.4.3.154.oe2403sp4.x86_64-eulerpilot-scx`
- 轮数：`5`
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
| GET | 35075.634 | 0.479 | 424.475 | 35559.110 | 0.425 | 423.025 | 17245.446 | 3.220 | 660.350 | 32695.928 | 1.588 | 558.775 | 18245.054 | 3.125 | 655.125 | 17969.438 | 3.290 | 652.450 | 10454.576 | 5.250 | 1376.400 |
| INCR | 35806.790 | 0.418 | 424.475 | 34929.668 | 0.460 | 423.025 | 17416.314 | 3.092 | 660.350 | 33972.864 | 1.298 | 558.775 | 18301.026 | 3.132 | 655.125 | 18383.330 | 3.085 | 652.450 | 10488.930 | 4.965 | 1376.400 |
| PING_INLINE | 30076.430 | 0.866 | 424.475 | 30246.894 | 0.785 | 423.025 | 17987.184 | 2.917 | 660.350 | 18048.484 | 3.469 | 558.775 | 17793.246 | 3.623 | 655.125 | 18043.500 | 3.548 | 652.450 | 10831.224 | 5.289 | 1376.400 |
| SET | 35638.978 | 0.433 | 424.475 | 35888.246 | 0.433 | 423.025 | 18517.480 | 2.794 | 660.350 | 29190.392 | 2.361 | 558.775 | 18281.270 | 3.009 | 655.125 | 18359.914 | 3.053 | 652.450 | 10314.552 | 4.977 | 1376.400 |

## 相对 `noisy_default` 的自动观察

### GET
- `quiet_default`：RPS 103.39%，P99 -85.12%
- `quiet_scx_normal`：RPS 106.19%，P99 -86.80%
- `noisy_cgroup_v2`：RPS 89.59%，P99 -50.68%
- `noisy_scx_normal`：RPS 5.80%，P99 -2.95%
- `noisy_scx_always_active`：RPS 4.20%，P99 2.17%
- `noisy_scx_psi`：RPS -39.38%，P99 63.04%
### INCR
- `quiet_default`：RPS 105.59%，P99 -86.48%
- `quiet_scx_normal`：RPS 100.56%，P99 -85.12%
- `noisy_cgroup_v2`：RPS 95.06%，P99 -58.02%
- `noisy_scx_normal`：RPS 5.08%，P99 1.29%
- `noisy_scx_always_active`：RPS 5.55%，P99 -0.23%
- `noisy_scx_psi`：RPS -39.78%，P99 60.58%
### PING_INLINE
- `quiet_default`：RPS 67.21%，P99 -70.31%
- `quiet_scx_normal`：RPS 68.16%，P99 -73.09%
- `noisy_cgroup_v2`：RPS 0.34%，P99 18.92%
- `noisy_scx_normal`：RPS -1.08%，P99 24.20%
- `noisy_scx_always_active`：RPS 0.31%，P99 21.63%
- `noisy_scx_psi`：RPS -39.78%，P99 81.32%
### SET
- `quiet_default`：RPS 92.46%，P99 -84.50%
- `quiet_scx_normal`：RPS 93.81%，P99 -84.50%
- `noisy_cgroup_v2`：RPS 57.64%，P99 -15.50%
- `noisy_scx_normal`：RPS -1.28%，P99 7.70%
- `noisy_scx_always_active`：RPS -0.85%，P99 9.27%
- `noisy_scx_psi`：RPS -44.30%，P99 78.13%

## 运行边界

- `redis-server` 为被保护服务，`stress-ng` 为后台干扰任务。
- `redis-benchmark` 固定留在默认根组，不参与分类控制。
- `cgroup_v2` 组运行前必须保证 `sched_ext state=disabled`。
- `sched_ext` 组运行时必须保证 `sched_ext state=enabled`，并保留 gate 状态与统计快照。

## 当前结论

- 本报告用于正式后端对照阶段的第一轮汇总，后续还需要继续扩大 `RUNS` 并补图表。
