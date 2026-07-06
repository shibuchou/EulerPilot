# Nginx sched_ext 后端正式对照报告

## 运行信息

- 结果目录：`/root/EulerPilot/results/final/nginx-scx-compare-20260706-101928`
- 主机：`cernet2.net`
- 内核：`6.6.0-159.4.3.154.oe2403sp4.x86_64-eulerpilot-scx`
- 轮数：`1`
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

| 组别 | Requests/sec | P99(ms) |
| --- | ---: | ---: |
| quiet_default | 21652.820 | 2.750 |
| quiet_scx_normal | 13922.940 | 26.170 |
| noisy_default | 12701.270 | 5.120 |
| noisy_cgroup_v2 | 15859.220 | 4.200 |
| noisy_scx_normal | 6184.880 | 5.060 |
| noisy_scx_always_active | 9998.230 | 1030.000 |
| noisy_scx_psi | 6032.460 | 5.470 |

## 相对 `noisy_default` 的自动观察

- `quiet_default`：RPS 70.48%，P99 -46.29%
- `quiet_scx_normal`：RPS 9.62%，P99 411.13%
- `noisy_cgroup_v2`：RPS 24.86%，P99 -17.97%
- `noisy_scx_normal`：RPS -51.31%，P99 -1.17%
- `noisy_scx_always_active`：RPS -21.28%，P99 20017.19%
- `noisy_scx_psi`：RPS -52.51%，P99 6.84%

## 当前结论

- 本报告用于 Nginx sched_ext 正式对照的第一轮汇总，后续还需要扩大 `RUNS` 并补图表。
