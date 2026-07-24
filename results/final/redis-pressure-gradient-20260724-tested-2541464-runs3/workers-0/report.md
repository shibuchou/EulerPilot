# Redis sched_ext 后端正式对照报告

## 运行信息

- 结果目录：`/root/eulerpilot-runs/2541464552aa763522a8496a5082a514a843a179/formal-20260723-153923/redis-pressure-gradient-runs3/workers-0`
- 主机：`cernet2.net`
- 内核：`6.6.0-159.4.3.154.oe2403sp4.x86_64-eulerpilot-scx`
- 轮数：`3`
- Redis 端口：`6386`
- bench clients：`16`
- bench requests：`20000`
- stress workers：`0`
- sched_ext switch mode：`full`

## 组别说明

- `quiet_default`：仅 Redis，默认调度器
- `noisy_default`：Redis + stress-ng，默认调度器
- `noisy_cgroup_v2`：Redis + stress-ng，cgroup v2 控制
- `noisy_scx_psi`：Redis + stress-ng，sched_ext psi

## 汇总表

| 测试项 | quiet_default RPS | quiet_default P99(ms) | quiet_default CPU/10k | noisy_default RPS | noisy_default P99(ms) | noisy_default CPU/10k | noisy_cgroup_v2 RPS | noisy_cgroup_v2 P99(ms) | noisy_cgroup_v2 CPU/10k | noisy_scx_psi RPS | noisy_scx_psi P99(ms) | noisy_scx_psi CPU/10k |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| GET | 35026.843 | 0.490 | 424.458 | 36171.210 | 0.402 | 418.083 | 36342.037 | 0.423 | 420.750 | 8531.840 | 3.452 | 1582.792 |
| INCR | 36213.230 | 0.415 | 424.458 | 36063.240 | 0.399 | 418.083 | 35969.983 | 0.455 | 420.750 | 9126.510 | 3.468 | 1582.792 |
| PING_INLINE | 33393.157 | 0.751 | 424.458 | 32207.300 | 0.951 | 418.083 | 30881.583 | 0.970 | 420.750 | 2585.437 | 4.615 | 1582.792 |
| SET | 36453.410 | 0.498 | 424.458 | 35976.147 | 0.423 | 418.083 | 36426.497 | 0.407 | 420.750 | 7453.553 | 3.687 | 1582.792 |

## 相对 `noisy_default` 的自动观察

### GET
- `quiet_default`：RPS -3.16%，P99 21.89%
- `noisy_cgroup_v2`：RPS 0.47%，P99 5.22%
- `noisy_scx_psi`：RPS -76.41%，P99 758.71%
### INCR
- `quiet_default`：RPS 0.42%，P99 4.01%
- `noisy_cgroup_v2`：RPS -0.26%，P99 14.04%
- `noisy_scx_psi`：RPS -74.69%，P99 769.17%
### PING_INLINE
- `quiet_default`：RPS 3.68%，P99 -21.03%
- `noisy_cgroup_v2`：RPS -4.12%，P99 2.00%
- `noisy_scx_psi`：RPS -91.97%，P99 385.28%
### SET
- `quiet_default`：RPS 1.33%，P99 17.73%
- `noisy_cgroup_v2`：RPS 1.25%，P99 -3.78%
- `noisy_scx_psi`：RPS -79.28%，P99 771.63%

## 运行边界

- `redis-server` 为被保护服务，`stress-ng` 为后台干扰任务。
- `redis-benchmark` 固定留在默认根组，不参与分类控制。
- `cgroup_v2` 组运行前必须保证 `sched_ext state=disabled`。
- `sched_ext` 组运行时必须保证 `sched_ext state=enabled`，并保留 gate 状态与统计快照。

## 当前结论

- 本报告用于正式后端对照阶段的第一轮汇总，后续还需要继续扩大 `RUNS` 并补图表。
