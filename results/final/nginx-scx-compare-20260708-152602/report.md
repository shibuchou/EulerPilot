# Nginx sched_ext 后端正式对照报告

## 运行信息

- 结果目录：`/root/EulerPilot/results/final/nginx-scx-compare-20260708-152602`
- 主机：`cernet2.net`
- 内核：`6.6.0-159.4.3.154.oe2403sp4.x86_64-eulerpilot-scx`
- 轮数：`5`
- Nginx 端口：`18082`
- wrk threads：`2`
- wrk connections：`32`
- wrk duration：`10s`
- stress workers：`2`

## 组别说明

- `quiet_default`：仅 Nginx，默认调度器
- `quiet_scx_normal`：仅 Nginx，sched_ext 常驻但保持 normal
- `noisy_default`：Nginx + stress-ng，默认调度器
- `noisy_cgroup_v2`：Nginx + stress-ng，cgroup v2 控制
- `noisy_scx_normal`：Nginx + stress-ng，sched_ext normal
- `noisy_scx_always_active`：Nginx + stress-ng，sched_ext always-active
- `noisy_scx_psi`：Nginx + stress-ng，sched_ext psi

## 汇总表

| 组别 | Requests/sec | P99(ms) | CPU/10k requests |
| --- | ---: | ---: | ---: |
| quiet_default | 22344.040 | 2.950 | 82.433 |
| quiet_scx_normal | 22517.994 | 2.802 | 81.999 |
| noisy_default | 15921.544 | 4.152 | 127.608 |
| noisy_cgroup_v2 | 16265.772 | 3.966 | 119.911 |
| noisy_scx_normal | 17084.288 | 4.374 | 117.123 |
| noisy_scx_always_active | 16940.676 | 4.352 | 118.492 |
| noisy_scx_psi | 16997.658 | 4.150 | 117.682 |

## 相对 `noisy_default` 的自动观察

- `quiet_default`：RPS 40.34%，P99 -28.95%
- `quiet_scx_normal`：RPS 41.43%，P99 -32.51%
- `noisy_cgroup_v2`：RPS 2.16%，P99 -4.48%
- `noisy_scx_normal`：RPS 7.30%，P99 5.35%
- `noisy_scx_always_active`：RPS 6.40%，P99 4.82%
- `noisy_scx_psi`：RPS 6.76%，P99 -0.05%

## 当前结论

- 本报告用于 Nginx sched_ext 正式对照的第一轮汇总，后续还需要扩大 `RUNS` 并补图表。
