# Redis sched_ext 后端正式对照报告

## 运行信息

- 结果目录：`/root/EulerPilot/results/final/redis-pressure-gradient-20260708-153811/workers-2`
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
| GET | 35697.353 | 0.471 | 427.458 | 17150.813 | 3.234 | 663.958 | 34357.107 | 1.516 | 556.542 | 10304.917 | 5.167 | 1438.875 |
| INCR | 31951.307 | 0.516 | 427.458 | 17540.767 | 3.087 | 663.958 | 32634.860 | 1.444 | 556.542 | 11021.477 | 4.748 | 1438.875 |
| PING_INLINE | 26416.167 | 0.812 | 427.458 | 17301.350 | 3.108 | 663.958 | 18824.750 | 2.948 | 556.542 | 10428.787 | 5.591 | 1438.875 |
| SET | 35514.620 | 0.444 | 427.458 | 16470.380 | 3.151 | 663.958 | 28914.373 | 2.026 | 556.542 | 10543.590 | 5.143 | 1438.875 |

## 相对 `noisy_default` 的自动观察

### GET
- `quiet_default`：RPS 108.14%，P99 -85.44%
- `noisy_cgroup_v2`：RPS 100.32%，P99 -53.12%
- `noisy_scx_psi`：RPS -39.92%，P99 59.77%
### INCR
- `quiet_default`：RPS 82.15%，P99 -83.28%
- `noisy_cgroup_v2`：RPS 86.05%，P99 -53.22%
- `noisy_scx_psi`：RPS -37.17%，P99 53.81%
### PING_INLINE
- `quiet_default`：RPS 52.68%，P99 -73.87%
- `noisy_cgroup_v2`：RPS 8.81%，P99 -5.15%
- `noisy_scx_psi`：RPS -39.72%，P99 79.89%
### SET
- `quiet_default`：RPS 115.63%，P99 -85.91%
- `noisy_cgroup_v2`：RPS 75.55%，P99 -35.70%
- `noisy_scx_psi`：RPS -35.98%，P99 63.22%

## 运行边界

- `redis-server` 为被保护服务，`stress-ng` 为后台干扰任务。
- `redis-benchmark` 固定留在默认根组，不参与分类控制。
- `cgroup_v2` 组运行前必须保证 `sched_ext state=disabled`。
- `sched_ext` 组运行时必须保证 `sched_ext state=enabled`，并保留 gate 状态与统计快照。

## 当前结论

- 本报告用于正式后端对照阶段的第一轮汇总，后续还需要继续扩大 `RUNS` 并补图表。
