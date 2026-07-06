# Nginx sched_ext 后端正式对照报告

## 运行信息

- 结果目录：`/root/EulerPilot/results/final/nginx-scx-compare-20260706-120547`
- 主机：`cernet2.net`
- 内核：`6.6.0-159.4.3.154.oe2403sp4.x86_64-eulerpilot-scx`
- 轮数：`3`
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
| quiet_default | 22138.920 | 3.037 |
| quiet_scx_normal | 13182.823 | 11.007 |
| noisy_default | 15203.393 | 4.583 |
| noisy_cgroup_v2 | 16060.380 | 4.257 |
| noisy_scx_normal | 6353.597 | 5.090 |
| noisy_scx_always_active | 9693.740 | 1050.230 |
| noisy_scx_psi | 6750.803 | 4.680 |

## 相对 `noisy_default` 的自动观察

- `quiet_default`：RPS 45.62%，P99 -33.73%
- `quiet_scx_normal`：RPS -13.29%，P99 140.17%
- `noisy_cgroup_v2`：RPS 5.64%，P99 -7.11%
- `noisy_scx_normal`：RPS -58.21%，P99 11.06%
- `noisy_scx_always_active`：RPS -36.24%，P99 22815.78%
- `noisy_scx_psi`：RPS -55.60%，P99 2.12%

## 当前结论

- 本报告用于 Nginx sched_ext 正式对照的第一轮汇总，后续还需要扩大 `RUNS` 并补图表。
