# Redis sched_ext 后端正式对照报告

## 运行信息

- 结果目录：`/root/eulerpilot-runs/2541464552aa763522a8496a5082a514a843a179/formal-20260723-153923/redis-pressure-gradient-runs3/workers-1`
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
| GET | 36459.303 | 0.394 | 417.833 | 20556.080 | 0.839 | 624.333 | 26015.433 | 0.866 | 593.250 | 9442.210 | 4.439 | 1578.167 |
| INCR | 36640.323 | 0.423 | 417.833 | 20390.620 | 0.898 | 624.333 | 31190.730 | 0.866 | 593.250 | 9729.077 | 4.306 | 1578.167 |
| PING_INLINE | 31352.967 | 1.204 | 417.833 | 19743.970 | 2.018 | 624.333 | 19385.333 | 1.922 | 593.250 | 4339.560 | 7.970 | 1578.167 |
| SET | 36550.817 | 0.402 | 417.833 | 20026.840 | 0.879 | 624.333 | 19661.870 | 0.887 | 593.250 | 9424.673 | 4.268 | 1578.167 |

## 相对 `noisy_default` 的自动观察

### GET
- `quiet_default`：RPS 77.37%，P99 -53.04%
- `noisy_cgroup_v2`：RPS 26.56%，P99 3.22%
- `noisy_scx_psi`：RPS -54.07%，P99 429.08%
### INCR
- `quiet_default`：RPS 79.69%，P99 -52.90%
- `noisy_cgroup_v2`：RPS 52.97%，P99 -3.56%
- `noisy_scx_psi`：RPS -52.29%，P99 379.51%
### PING_INLINE
- `quiet_default`：RPS 58.80%，P99 -40.34%
- `noisy_cgroup_v2`：RPS -1.82%，P99 -4.76%
- `noisy_scx_psi`：RPS -78.02%，P99 294.95%
### SET
- `quiet_default`：RPS 82.51%，P99 -54.27%
- `noisy_cgroup_v2`：RPS -1.82%，P99 0.91%
- `noisy_scx_psi`：RPS -52.94%，P99 385.55%

## 运行边界

- `redis-server` 为被保护服务，`stress-ng` 为后台干扰任务。
- `redis-benchmark` 固定留在默认根组，不参与分类控制。
- `cgroup_v2` 组运行前必须保证 `sched_ext state=disabled`。
- `sched_ext` 组运行时必须保证 `sched_ext state=enabled`，并保留 gate 状态与统计快照。

## 当前结论

- 本报告用于正式后端对照阶段的第一轮汇总，后续还需要继续扩大 `RUNS` 并补图表。
