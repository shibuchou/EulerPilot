# Nginx sched_ext 后端正式对照报告

## 运行信息

- 结果目录：`/root/eulerpilot-runs/2541464552aa763522a8496a5082a514a843a179/formal-20260723-153923/nginx-scx-compare-runs10`
- 主机：`cernet2.net`
- 内核：`6.6.0-159.4.3.154.oe2403sp4.x86_64-eulerpilot-scx`
- 轮数：`10`
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
| quiet_default | 22600.675 | 2.269 | 81.770 |
| quiet_scx_normal | 15206.129 | 2.709 | 84.887 |
| noisy_default | 16644.758 | 3.856 | 121.367 |
| noisy_cgroup_v2 | 16200.689 | 3.832 | 121.186 |
| noisy_scx_normal | 11742.610 | 4.051 | 121.691 |
| noisy_scx_always_active | 13480.981 | 9.971 | 149.217 |
| noisy_scx_psi | 10207.239 | 942.330 | 178.923 |

## 相对 `noisy_default` 的自动观察

- `quiet_default`：RPS 35.78%，P99 -41.16%
- `quiet_scx_normal`：RPS -8.64%，P99 -29.75%
- `noisy_cgroup_v2`：RPS -2.67%，P99 -0.62%
- `noisy_scx_normal`：RPS -29.45%，P99 5.06%
- `noisy_scx_always_active`：RPS -19.01%，P99 158.58%
- `noisy_scx_psi`：RPS -38.68%，P99 24338.02%

## 当前结论

- 本报告用于 Nginx sched_ext 正式对照的第一轮汇总，后续还需要扩大 `RUNS` 并补图表。
