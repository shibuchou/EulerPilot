# Redis sched_ext 后端正式对照报告

## 运行信息

- 结果目录：`/root/EulerPilot/results/final/redis-pressure-gradient-20260708-153811/workers-1`
- 主机：`cernet2.net`
- 内核：`6.6.0-159.4.3.154.oe2403sp4.x86_64-eulerpilot-scx`
- 轮数：`3`
- Redis 端口：`6386`
- bench clients：`16`
- bench requests：`20000`
- stress workers：`1`
- sched_ext switch mode：`full`

## 组别说明

- `quiet_default`：仅 Redis，默认调度器
- `noisy_default`：Redis + stress-ng，默认调度器
- `noisy_cgroup_v2`：Redis + stress-ng，cgroup v2 控制
- `noisy_scx_psi`：Redis + stress-ng，sched_ext psi

## 汇总表

| 测试项 | quiet_default RPS | quiet_default P99(ms) | quiet_default CPU/10k | noisy_default RPS | noisy_default P99(ms) | noisy_default CPU/10k | noisy_cgroup_v2 RPS | noisy_cgroup_v2 P99(ms) | noisy_cgroup_v2 CPU/10k | noisy_scx_psi RPS | noisy_scx_psi P99(ms) | noisy_scx_psi CPU/10k |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| GET | 35622.107 | 0.474 | 420.750 | 20605.703 | 1.098 | 620.208 | 31043.893 | 0.871 | 573.083 | 11165.753 | 4.295 | 1232.167 |
| INCR | 35980.797 | 0.460 | 420.750 | 20530.383 | 0.908 | 620.208 | 32571.923 | 0.786 | 573.083 | 11301.833 | 4.290 | 1232.167 |
| PING_INLINE | 33595.660 | 0.738 | 420.750 | 19664.950 | 1.324 | 620.208 | 18609.357 | 1.874 | 573.083 | 11001.313 | 4.378 | 1232.167 |
| SET | 36151.960 | 0.460 | 420.750 | 20422.300 | 1.159 | 620.208 | 25794.067 | 1.098 | 573.083 | 11136.263 | 4.356 | 1232.167 |

## 相对 `noisy_default` 的自动观察

### GET
- `quiet_default`：RPS 72.87%，P99 -56.83%
- `noisy_cgroup_v2`：RPS 50.66%，P99 -20.67%
- `noisy_scx_psi`：RPS -45.81%，P99 291.17%
### INCR
- `quiet_default`：RPS 75.26%，P99 -49.34%
- `noisy_cgroup_v2`：RPS 58.65%，P99 -13.44%
- `noisy_scx_psi`：RPS -44.95%，P99 372.47%
### PING_INLINE
- `quiet_default`：RPS 70.84%，P99 -44.26%
- `noisy_cgroup_v2`：RPS -5.37%，P99 41.54%
- `noisy_scx_psi`：RPS -44.06%，P99 230.66%
### SET
- `quiet_default`：RPS 77.02%，P99 -60.31%
- `noisy_cgroup_v2`：RPS 26.30%，P99 -5.26%
- `noisy_scx_psi`：RPS -45.47%，P99 275.84%

## 运行边界

- `redis-server` 为被保护服务，`stress-ng` 为后台干扰任务。
- `redis-benchmark` 固定留在默认根组，不参与分类控制。
- `cgroup_v2` 组运行前必须保证 `sched_ext state=disabled`。
- `sched_ext` 组运行时必须保证 `sched_ext state=enabled`，并保留 gate 状态与统计快照。

## 当前结论

- 本报告用于正式后端对照阶段的第一轮汇总，后续还需要继续扩大 `RUNS` 并补图表。
