# Redis sched_ext 后端正式对照报告

## 运行信息

- 结果目录：`/root/EulerPilot/results/final/redis-pressure-gradient-20260708-153811/workers-0`
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
| GET | 35536.910 | 0.532 | 421.292 | 35843.747 | 0.428 | 422.958 | 35612.683 | 0.426 | 426.458 | 7175.747 | 3.586 | 1532.500 |
| INCR | 35648.827 | 0.418 | 421.292 | 36169.750 | 0.418 | 422.958 | 35254.340 | 0.498 | 426.458 | 7762.697 | 3.623 | 1532.500 |
| PING_INLINE | 31305.787 | 1.084 | 421.292 | 33044.443 | 0.738 | 422.958 | 30001.430 | 0.759 | 426.458 | 7950.733 | 4.218 | 1532.500 |
| SET | 35671.127 | 0.503 | 421.292 | 35453.527 | 0.522 | 422.958 | 35670.763 | 0.415 | 426.458 | 7644.853 | 4.031 | 1532.500 |

## 相对 `noisy_default` 的自动观察

### GET
- `quiet_default`：RPS -0.86%，P99 24.30%
- `noisy_cgroup_v2`：RPS -0.64%，P99 -0.47%
- `noisy_scx_psi`：RPS -79.98%，P99 737.85%
### INCR
- `quiet_default`：RPS -1.44%，P99 0.00%
- `noisy_cgroup_v2`：RPS -2.53%，P99 19.14%
- `noisy_scx_psi`：RPS -78.54%，P99 766.75%
### PING_INLINE
- `quiet_default`：RPS -5.26%，P99 46.88%
- `noisy_cgroup_v2`：RPS -9.21%，P99 2.85%
- `noisy_scx_psi`：RPS -75.94%，P99 471.54%
### SET
- `quiet_default`：RPS 0.61%，P99 -3.64%
- `noisy_cgroup_v2`：RPS 0.61%，P99 -20.50%
- `noisy_scx_psi`：RPS -78.44%，P99 672.22%

## 运行边界

- `redis-server` 为被保护服务，`stress-ng` 为后台干扰任务。
- `redis-benchmark` 固定留在默认根组，不参与分类控制。
- `cgroup_v2` 组运行前必须保证 `sched_ext state=disabled`。
- `sched_ext` 组运行时必须保证 `sched_ext state=enabled`，并保留 gate 状态与统计快照。

## 当前结论

- 本报告用于正式后端对照阶段的第一轮汇总，后续还需要继续扩大 `RUNS` 并补图表。
