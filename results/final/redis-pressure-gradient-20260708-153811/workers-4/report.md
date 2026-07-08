# Redis sched_ext 后端正式对照报告

## 运行信息

- 结果目录：`/root/EulerPilot/results/final/redis-pressure-gradient-20260708-153811/workers-4`
- 主机：`cernet2.net`
- 内核：`6.6.0-159.4.3.154.oe2403sp4.x86_64-eulerpilot-scx`
- 轮数：`3`
- Redis 端口：`6386`
- bench clients：`16`
- bench requests：`20000`
- stress workers：`4`
- sched_ext switch mode：`full`

## 组别说明

- `quiet_default`：仅 Redis，默认调度器
- `noisy_default`：Redis + stress-ng，默认调度器
- `noisy_cgroup_v2`：Redis + stress-ng，cgroup v2 控制
- `noisy_scx_psi`：Redis + stress-ng，sched_ext psi

## 汇总表

| 测试项 | quiet_default RPS | quiet_default P99(ms) | quiet_default CPU/10k | noisy_default RPS | noisy_default P99(ms) | noisy_default CPU/10k | noisy_cgroup_v2 RPS | noisy_cgroup_v2 P99(ms) | noisy_cgroup_v2 CPU/10k | noisy_scx_psi RPS | noisy_scx_psi P99(ms) | noisy_scx_psi CPU/10k |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| GET | 35961.930 | 0.484 | 422.125 | 11797.980 | 4.362 | 741.833 | 32710.083 | 1.658 | 563.417 | 11589.953 | 5.714 | 1418.208 |
| INCR | 36297.960 | 0.439 | 422.125 | 12559.880 | 4.514 | 741.833 | 34019.760 | 1.460 | 563.417 | 7916.023 | 7.556 | 1418.208 |
| PING_INLINE | 31524.263 | 1.188 | 422.125 | 13199.150 | 4.503 | 741.833 | 12770.947 | 5.154 | 563.417 | 10120.977 | 7.122 | 1418.208 |
| SET | 36049.853 | 0.503 | 422.125 | 11074.713 | 4.991 | 741.833 | 34338.297 | 1.127 | 563.417 | 9615.007 | 6.860 | 1418.208 |

## 相对 `noisy_default` 的自动观察

### GET
- `quiet_default`：RPS 204.81%，P99 -88.90%
- `noisy_cgroup_v2`：RPS 177.25%，P99 -61.99%
- `noisy_scx_psi`：RPS -1.76%，P99 30.99%
### INCR
- `quiet_default`：RPS 189.00%，P99 -90.27%
- `noisy_cgroup_v2`：RPS 170.86%，P99 -67.66%
- `noisy_scx_psi`：RPS -36.97%，P99 67.39%
### PING_INLINE
- `quiet_default`：RPS 138.84%，P99 -73.62%
- `noisy_cgroup_v2`：RPS -3.24%，P99 14.46%
- `noisy_scx_psi`：RPS -23.32%，P99 58.16%
### SET
- `quiet_default`：RPS 225.52%，P99 -89.92%
- `noisy_cgroup_v2`：RPS 210.06%，P99 -77.42%
- `noisy_scx_psi`：RPS -13.18%，P99 37.45%

## 运行边界

- `redis-server` 为被保护服务，`stress-ng` 为后台干扰任务。
- `redis-benchmark` 固定留在默认根组，不参与分类控制。
- `cgroup_v2` 组运行前必须保证 `sched_ext state=disabled`。
- `sched_ext` 组运行时必须保证 `sched_ext state=enabled`，并保留 gate 状态与统计快照。

## 当前结论

- 本报告用于正式后端对照阶段的第一轮汇总，后续还需要继续扩大 `RUNS` 并补图表。
