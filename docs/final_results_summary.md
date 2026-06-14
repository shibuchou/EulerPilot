# EulerPilot 最终结果摘要

更新时间：`2026-06-14`

## 1. 最强候选结果目录

- Redis：`/root/EulerPilot/results/final/redis-scx-compare-20260612-191543`
- Nginx：`/root/EulerPilot/results/final/nginx-scx-compare-20260612-194018`

均满足：RUNS=5、平衡轮换、run_manifest.json、無 invalid_run、中文报告

## 2. Redis 结果

正式矩阵：quiet_default / quiet_scx_normal / noisy_default / noisy_cgroup_v2 / noisy_scx_normal / noisy_scx_always_active / noisy_scx_psi

观察：
- noisy_cgroup_v2 在 GET 上吞吐明显提升
- noisy_scx_normal 在 GET/INCR/SET 上 RPS 改善
- noisy_scx_psi 在 GET 上有一定正向趋势
- noisy_scx_always_active 不稳定优于其他模式

## 3. Nginx 结果

- noisy_cgroup_v2 表现更稳
- noisy_scx_psi 吞吐接近 noisy_default，但 P99 偏高
- quiet_scx_normal 存在不可忽略的基础开销

## 4. 关键证据链

1. latency + background 场景前提成立
2. PsiGate 进入 ACTIVE
3. cgroup_v2 存在 applied=yes / sched_ext 存在 executor=sched_ext
4. 业务结果写入正式候选目录

## 5. 图表（7 张 SVG）
redis_sched_ext_rps/p99/quiet_overhead + nginx_sched_ext_rps/p99/quiet_overhead + psigate_timeline

## 6. 可提交边界

已支撑：统一 Agent 架构、双执行后端、Redis/Nginx 正式结果、Skills 框架、network/security demo、中文报告、图表材料

不建议写成：sched_ext 全面优于默认调度器、所有场景都取得稳定收益

项目代码：`https://github.com/shibuchou/EulerPilot`
