# Redis sched_ext 后端正式对照报告

## 运行信息

- 结果目录：`/root/eulerpilot-runs/2541464552aa763522a8496a5082a514a843a179/formal-20260723-153923/redis-pressure-gradient-runs3/workers-4`
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
| GET | 36607.880 | 0.383 | 417.792 | 12319.077 | 4.850 | 733.125 | 29105.007 | 1.871 | 616.583 | 9143.727 | 5.492 | 1759.917 |
| INCR | 35992.980 | 0.500 | 417.792 | 12724.317 | 4.311 | 733.125 | 30620.287 | 1.663 | 616.583 | 11459.587 | 4.764 | 1759.917 |
| PING_INLINE | 31617.353 | 1.028 | 417.792 | 13512.603 | 4.412 | 733.125 | 11301.093 | 5.522 | 616.583 | 3543.283 | 9.332 | 1759.917 |
| SET | 36239.817 | 0.434 | 417.792 | 11482.840 | 5.015 | 733.125 | 25792.750 | 2.495 | 616.583 | 7884.523 | 12.234 | 1759.917 |

## 相对 `noisy_default` 的自动观察

### GET
- `quiet_default`：RPS 197.16%，P99 -92.10%
- `noisy_cgroup_v2`：RPS 136.26%，P99 -61.42%
- `noisy_scx_psi`：RPS -25.78%，P99 13.24%
### INCR
- `quiet_default`：RPS 182.87%，P99 -88.40%
- `noisy_cgroup_v2`：RPS 140.64%，P99 -61.42%
- `noisy_scx_psi`：RPS -9.94%，P99 10.51%
### PING_INLINE
- `quiet_default`：RPS 133.98%，P99 -76.70%
- `noisy_cgroup_v2`：RPS -16.37%，P99 25.16%
- `noisy_scx_psi`：RPS -73.78%，P99 111.51%
### SET
- `quiet_default`：RPS 215.60%，P99 -91.35%
- `noisy_cgroup_v2`：RPS 124.62%，P99 -50.25%
- `noisy_scx_psi`：RPS -31.34%，P99 143.95%

## 运行边界

- `redis-server` 为被保护服务，`stress-ng` 为后台干扰任务。
- `redis-benchmark` 固定留在默认根组，不参与分类控制。
- `cgroup_v2` 组运行前必须保证 `sched_ext state=disabled`。
- `sched_ext` 组运行时必须保证 `sched_ext state=enabled`，并保留 gate 状态与统计快照。

## 当前结论

- 本报告用于正式后端对照阶段的第一轮汇总，后续还需要继续扩大 `RUNS` 并补图表。
