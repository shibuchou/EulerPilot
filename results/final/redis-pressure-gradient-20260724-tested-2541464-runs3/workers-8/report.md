# Redis sched_ext 后端正式对照报告

## 运行信息

- 结果目录：`/root/eulerpilot-runs/2541464552aa763522a8496a5082a514a843a179/formal-20260723-153923/redis-pressure-gradient-runs3/workers-8`
- 主机：`cernet2.net`
- 内核：`6.6.0-159.4.3.154.oe2403sp4.x86_64-eulerpilot-scx`
- 轮数：`3`
- Redis 端口：`6386`
- bench clients：`16`
- bench requests：`20000`
- stress workers：`8`
- sched_ext switch mode：`full`

## 组别说明

- `quiet_default`：仅 Redis，默认调度器
- `noisy_default`：Redis + stress-ng，默认调度器
- `noisy_cgroup_v2`：Redis + stress-ng，cgroup v2 控制
- `noisy_scx_psi`：Redis + stress-ng，sched_ext psi

## 汇总表

| 测试项 | quiet_default RPS | quiet_default P99(ms) | quiet_default CPU/10k | noisy_default RPS | noisy_default P99(ms) | noisy_default CPU/10k | noisy_cgroup_v2 RPS | noisy_cgroup_v2 P99(ms) | noisy_cgroup_v2 CPU/10k | noisy_scx_psi RPS | noisy_scx_psi P99(ms) | noisy_scx_psi CPU/10k |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| GET | 36743.963 | 0.404 | 415.875 | 9292.700 | 5.439 | 778.000 | 33291.173 | 1.295 | 607.042 | 8503.623 | 5.554 | 1923.292 |
| INCR | 36456.670 | 0.439 | 415.875 | 10347.587 | 4.956 | 778.000 | 33289.693 | 1.586 | 607.042 | 7236.677 | 3.511 | 1923.292 |
| PING_INLINE | 31962.677 | 1.002 | 415.875 | 7954.913 | 7.871 | 778.000 | 9048.667 | 6.663 | 607.042 | 4258.257 | 10.178 | 1923.292 |
| SET | 36612.160 | 0.407 | 415.875 | 9143.820 | 5.298 | 778.000 | 28595.350 | 1.986 | 607.042 | 4915.043 | 7.906 | 1923.292 |

## 相对 `noisy_default` 的自动观察

### GET
- `quiet_default`：RPS 295.41%，P99 -92.57%
- `noisy_cgroup_v2`：RPS 258.25%，P99 -76.19%
- `noisy_scx_psi`：RPS -8.49%，P99 2.11%
### INCR
- `quiet_default`：RPS 252.32%，P99 -91.14%
- `noisy_cgroup_v2`：RPS 221.71%，P99 -68.00%
- `noisy_scx_psi`：RPS -30.06%，P99 -29.16%
### PING_INLINE
- `quiet_default`：RPS 301.80%，P99 -87.27%
- `noisy_cgroup_v2`：RPS 13.75%，P99 -15.35%
- `noisy_scx_psi`：RPS -46.47%，P99 29.31%
### SET
- `quiet_default`：RPS 300.40%，P99 -92.32%
- `noisy_cgroup_v2`：RPS 212.73%，P99 -62.51%
- `noisy_scx_psi`：RPS -46.25%，P99 49.23%

## 运行边界

- `redis-server` 为被保护服务，`stress-ng` 为后台干扰任务。
- `redis-benchmark` 固定留在默认根组，不参与分类控制。
- `cgroup_v2` 组运行前必须保证 `sched_ext state=disabled`。
- `sched_ext` 组运行时必须保证 `sched_ext state=enabled`，并保留 gate 状态与统计快照。

## 当前结论

- 本报告用于正式后端对照阶段的第一轮汇总，后续还需要继续扩大 `RUNS` 并补图表。
