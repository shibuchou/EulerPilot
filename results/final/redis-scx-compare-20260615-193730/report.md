# Redis sched_ext 后端正式对照报告

## 运行信息

- 结果目录：`/root/EulerPilot/results/final/redis-scx-compare-20260615-193730`
- 主机：`cernet2.net`
- 内核：`6.6.0-olk66-scx`
- 轮数：`15`
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

| 测试项 | quiet_default RPS | quiet_default P99(ms) | quiet_scx_normal RPS | quiet_scx_normal P99(ms) | noisy_default RPS | noisy_default P99(ms) | noisy_cgroup_v2 RPS | noisy_cgroup_v2 P99(ms) | noisy_scx_normal RPS | noisy_scx_normal P99(ms) | noisy_scx_always_active RPS | noisy_scx_always_active P99(ms) | noisy_scx_psi RPS | noisy_scx_psi P99(ms) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| GET | 34255.048 | 0.613 | 41360.297 | 0.592 | 36775.382 | 0.729 | 35794.576 | 0.781 | 39502.295 | 0.625 | 40558.871 | 0.592 | 41946.673 | 0.512 |
| INCR | 34347.631 | 0.588 | 46576.124 | 0.478 | 35270.209 | 0.765 | 36485.555 | 0.742 | 41613.811 | 0.532 | 41757.153 | 0.547 | 43562.682 | 0.565 |
| PING_INLINE | 33672.298 | 0.605 | 34146.975 | 0.622 | 35609.091 | 0.726 | 36926.169 | 0.706 | 37404.153 | 0.658 | 35991.280 | 0.693 | 37491.176 | 0.714 |
| SET | 34297.205 | 0.593 | 38175.923 | 0.614 | 36402.641 | 0.758 | 35305.252 | 0.793 | 37701.385 | 0.768 | 38294.688 | 0.681 | 39017.370 | 0.705 |

## 相对 `noisy_default` 的自动观察

### GET
- `quiet_default`：RPS -6.85%，P99 -15.91%
- `quiet_scx_normal`：RPS 12.47%，P99 -18.79%
- `noisy_cgroup_v2`：RPS -2.67%，P99 7.13%
- `noisy_scx_normal`：RPS 7.42%，P99 -14.27%
- `noisy_scx_always_active`：RPS 10.29%，P99 -18.79%
- `noisy_scx_psi`：RPS 14.06%，P99 -29.77%
### INCR
- `quiet_default`：RPS -2.62%，P99 -23.14%
- `quiet_scx_normal`：RPS 32.06%，P99 -37.52%
- `noisy_cgroup_v2`：RPS 3.45%，P99 -3.01%
- `noisy_scx_normal`：RPS 17.99%，P99 -30.46%
- `noisy_scx_always_active`：RPS 18.39%，P99 -28.50%
- `noisy_scx_psi`：RPS 23.51%，P99 -26.14%
### PING_INLINE
- `quiet_default`：RPS -5.44%，P99 -16.67%
- `quiet_scx_normal`：RPS -4.11%，P99 -14.33%
- `noisy_cgroup_v2`：RPS 3.70%，P99 -2.75%
- `noisy_scx_normal`：RPS 5.04%，P99 -9.37%
- `noisy_scx_always_active`：RPS 1.07%，P99 -4.55%
- `noisy_scx_psi`：RPS 5.29%，P99 -1.65%
### SET
- `quiet_default`：RPS -5.78%，P99 -21.77%
- `quiet_scx_normal`：RPS 4.87%，P99 -19.00%
- `noisy_cgroup_v2`：RPS -3.01%，P99 4.62%
- `noisy_scx_normal`：RPS 3.57%，P99 1.32%
- `noisy_scx_always_active`：RPS 5.20%，P99 -10.16%
- `noisy_scx_psi`：RPS 7.18%，P99 -6.99%

## 运行边界

- `redis-server` 为被保护服务，`stress-ng` 为后台干扰任务。
- `redis-benchmark` 固定留在默认根组，不参与分类控制。
- `cgroup_v2` 组运行前必须保证 `sched_ext state=disabled`。
- `sched_ext` 组运行时必须保证 `sched_ext state=enabled`，并保留 gate 状态与统计快照。

## 当前结论

- 本报告用于正式后端对照阶段的第一轮汇总，后续还需要继续扩大 `RUNS` 并补图表。
