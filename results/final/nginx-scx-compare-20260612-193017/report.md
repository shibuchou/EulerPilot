# Nginx sched_ext 后端正式对照报告

## 运行信息

- 结果目录：`/root/EulerPilot/results/final/nginx-scx-compare-20260612-193017`
- 主机：`cernet2.net`
- 内核：`6.6.0-olk66-scx`
- 轮数：`3`
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
| quiet_default | 33089.047 | 0.993 |
| quiet_scx_normal | 13541.643 | 4.927 |
| noisy_default | 35511.647 | 1.383 |
| noisy_cgroup_v2 | 35804.087 | 1.033 |
| noisy_scx_normal | 27335.097 | 3.423 |
| noisy_scx_always_active | 34914.940 | 25.250 |
| noisy_scx_psi | 35855.540 | 23.990 |

## 相对 `noisy_default` 的自动观察

- `quiet_default`：RPS -6.82%，P99 -28.20%
- `quiet_scx_normal`：RPS -61.87%，P99 256.25%
- `noisy_cgroup_v2`：RPS 0.82%，P99 -25.31%
- `noisy_scx_normal`：RPS -23.02%，P99 147.51%
- `noisy_scx_always_active`：RPS -1.68%，P99 1725.74%
- `noisy_scx_psi`：RPS 0.97%，P99 1634.63%

## 当前结论

- 本报告用于 Nginx sched_ext 正式对照的第一轮汇总，后续还需要扩大 `RUNS` 并补图表。
