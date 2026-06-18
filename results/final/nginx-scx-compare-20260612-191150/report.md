# Nginx sched_ext 后端正式对照报告

## 运行信息

- 结果目录：`/root/EulerPilot/results/final/nginx-scx-compare-20260612-191150`
- 主机：`cernet2.net`
- 内核：`6.6.0-olk66-scx`
- 轮数：`1`
- Nginx 端口：`18082`
- wrk threads：`2`
- wrk connections：`16`
- wrk duration：`5s`
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

| 组别 | Requests/sec | P99(ms) |
| --- | ---: | ---: |
| quiet_default | 36412.510 | 1.030 |
| quiet_scx_normal | 13657.120 | 5.290 |
| noisy_default | 35582.370 | 1.030 |
| noisy_cgroup_v2 | 38319.130 | 0.940 |
| noisy_scx_normal | 34990.410 | 2.910 |
| noisy_scx_always_active | 35620.430 | 22.490 |
| noisy_scx_psi | 35674.840 | 44.170 |

## 相对 `noisy_default` 的自动观察

- `quiet_default`：RPS 2.33%，P99 0.00%
- `quiet_scx_normal`：RPS -61.62%，P99 413.59%
- `noisy_cgroup_v2`：RPS 7.69%，P99 -8.74%
- `noisy_scx_normal`：RPS -1.66%，P99 182.52%
- `noisy_scx_always_active`：RPS 0.11%，P99 2083.50%
- `noisy_scx_psi`：RPS 0.26%，P99 4188.35%

## 当前结论

- 本报告用于 Nginx sched_ext 正式对照的第一轮汇总，后续还需要扩大 `RUNS` 并补图表。
