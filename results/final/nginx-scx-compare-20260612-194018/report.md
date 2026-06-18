# Nginx sched_ext 后端正式对照报告

## 运行信息

- 结果目录：`/root/EulerPilot/results/final/nginx-scx-compare-20260612-194018`
- 主机：`cernet2.net`
- 内核：`6.6.0-olk66-scx`
- 轮数：`5`
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
| quiet_default | 29707.964 | 1.212 |
| quiet_scx_normal | 13533.460 | 3.022 |
| noisy_default | 33526.336 | 1.102 |
| noisy_cgroup_v2 | 35872.500 | 1.102 |
| noisy_scx_normal | 30841.576 | 3.816 |
| noisy_scx_always_active | 33559.022 | 24.742 |
| noisy_scx_psi | 33859.416 | 17.126 |

## 相对 `noisy_default` 的自动观察

- `quiet_default`：RPS -11.39%，P99 9.98%
- `quiet_scx_normal`：RPS -59.63%，P99 174.23%
- `noisy_cgroup_v2`：RPS 7.00%，P99 0.00%
- `noisy_scx_normal`：RPS -8.01%，P99 246.28%
- `noisy_scx_always_active`：RPS 0.10%，P99 2145.19%
- `noisy_scx_psi`：RPS 0.99%，P99 1454.08%

## 当前结论

- 本报告用于 Nginx sched_ext 正式对照的第一轮汇总，后续还需要扩大 `RUNS` 并补图表。
