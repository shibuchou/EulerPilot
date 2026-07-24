# Redis sched_ext 后端正式对照报告

## 运行信息

- 结果目录：`/root/eulerpilot-runs/2541464552aa763522a8496a5082a514a843a179/formal-20260723-153923/redis-pressure-gradient-runs3/workers-2`
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
- `noisy_default`：Redis + stress-ng，默认调度器
- `noisy_cgroup_v2`：Redis + stress-ng，cgroup v2 控制
- `noisy_scx_psi`：Redis + stress-ng，sched_ext psi

## 汇总表

| 测试项 | quiet_default RPS | quiet_default P99(ms) | quiet_default CPU/10k | noisy_default RPS | noisy_default P99(ms) | noisy_default CPU/10k | noisy_cgroup_v2 RPS | noisy_cgroup_v2 P99(ms) | noisy_cgroup_v2 CPU/10k | noisy_scx_psi RPS | noisy_scx_psi P99(ms) | noisy_scx_psi CPU/10k |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| GET | 35861.587 | 0.418 | 419.625 | 16599.850 | 3.260 | 660.708 | 31157.923 | 2.276 | 588.167 | 10404.600 | 5.071 | 1528.708 |
| INCR | 35801.547 | 0.506 | 419.625 | 17787.070 | 3.282 | 660.708 | 32413.927 | 1.532 | 588.167 | 11080.307 | 4.802 | 1528.708 |
| PING_INLINE | 30606.683 | 0.986 | 419.625 | 17959.870 | 3.332 | 660.708 | 16208.210 | 3.476 | 588.167 | 11198.870 | 5.202 | 1528.708 |
| SET | 35912.460 | 0.410 | 419.625 | 17667.323 | 2.858 | 660.708 | 19095.673 | 3.055 | 588.167 | 10792.237 | 4.911 | 1528.708 |

## 相对 `noisy_default` 的自动观察

### GET
- `quiet_default`：RPS 116.04%，P99 -87.18%
- `noisy_cgroup_v2`：RPS 87.70%，P99 -30.18%
- `noisy_scx_psi`：RPS -37.32%，P99 55.55%
### INCR
- `quiet_default`：RPS 101.28%，P99 -84.58%
- `noisy_cgroup_v2`：RPS 82.23%，P99 -53.32%
- `noisy_scx_psi`：RPS -37.71%，P99 46.31%
### PING_INLINE
- `quiet_default`：RPS 70.42%，P99 -70.41%
- `noisy_cgroup_v2`：RPS -9.75%，P99 4.32%
- `noisy_scx_psi`：RPS -37.65%，P99 56.12%
### SET
- `quiet_default`：RPS 103.27%，P99 -85.65%
- `noisy_cgroup_v2`：RPS 8.08%，P99 6.89%
- `noisy_scx_psi`：RPS -38.91%，P99 71.83%

## 运行边界

- `redis-server` 为被保护服务，`stress-ng` 为后台干扰任务。
- `redis-benchmark` 固定留在默认根组，不参与分类控制。
- `cgroup_v2` 组运行前必须保证 `sched_ext state=disabled`。
- `sched_ext` 组运行时必须保证 `sched_ext state=enabled`，并保留 gate 状态与统计快照。

## 当前结论

- 本报告用于正式后端对照阶段的第一轮汇总，后续还需要继续扩大 `RUNS` 并补图表。
