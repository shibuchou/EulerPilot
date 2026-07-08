# Redis sched_ext 后端正式对照报告

## 运行信息

- 结果目录：`/root/EulerPilot/results/final/redis-pressure-gradient-20260708-153811/workers-8`
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
| GET | 35642.167 | 0.436 | 426.125 | 7740.483 | 7.042 | 793.208 | 33540.133 | 1.255 | 572.083 | 7348.103 | 9.188 | 1401.042 |
| INCR | 35449.093 | 0.410 | 426.125 | 7765.427 | 8.228 | 793.208 | 34582.433 | 1.196 | 572.083 | 7235.580 | 10.140 | 1401.042 |
| PING_INLINE | 30730.317 | 1.410 | 426.125 | 7861.063 | 8.151 | 793.208 | 8468.003 | 7.084 | 572.083 | 6613.020 | 11.148 | 1401.042 |
| SET | 35225.797 | 0.594 | 426.125 | 8620.653 | 7.866 | 793.208 | 32815.013 | 1.559 | 572.083 | 9381.103 | 6.831 | 1401.042 |

## 相对 `noisy_default` 的自动观察

### GET
- `quiet_default`：RPS 360.46%，P99 -93.81%
- `noisy_cgroup_v2`：RPS 333.31%，P99 -82.18%
- `noisy_scx_psi`：RPS -5.07%，P99 30.47%
### INCR
- `quiet_default`：RPS 356.50%，P99 -95.02%
- `noisy_cgroup_v2`：RPS 345.34%，P99 -85.46%
- `noisy_scx_psi`：RPS -6.82%，P99 23.24%
### PING_INLINE
- `quiet_default`：RPS 290.92%，P99 -82.70%
- `noisy_cgroup_v2`：RPS 7.72%，P99 -13.09%
- `noisy_scx_psi`：RPS -15.88%，P99 36.77%
### SET
- `quiet_default`：RPS 308.62%，P99 -92.45%
- `noisy_cgroup_v2`：RPS 280.66%，P99 -80.18%
- `noisy_scx_psi`：RPS 8.82%，P99 -13.16%

## 运行边界

- `redis-server` 为被保护服务，`stress-ng` 为后台干扰任务。
- `redis-benchmark` 固定留在默认根组，不参与分类控制。
- `cgroup_v2` 组运行前必须保证 `sched_ext state=disabled`。
- `sched_ext` 组运行时必须保证 `sched_ext state=enabled`，并保留 gate 状态与统计快照。

## 当前结论

- 本报告用于正式后端对照阶段的第一轮汇总，后续还需要继续扩大 `RUNS` 并补图表。
