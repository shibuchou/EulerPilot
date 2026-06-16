# EulerPilot 最终结果摘要

更新时间：\2026-06-15\

## 当前最终主结果

- Redis 主结果：\esults/final/redis-scx-compare-20260615-193730\
  - RUNS=15，4 operations x 7 scenarios，0 invalid files
- Redis 历史参考：\esults/final/redis-scx-compare-20260612-191543\
  - RUNS=5，不再作为正文主结论
- Nginx：\esults/final/nginx-scx-compare-20260612-194018\
  - 用于策略适配边界分析

## Redis RUNS=15 结果摘要

正式矩阵：quiet_default / quiet_scx_normal / noisy_default / noisy_cgroup_v2 / noisy_scx_normal / noisy_scx_always_active / noisy_scx_psi

观察：
- noisy_cgroup_v2 在 GET 上吞吐正向趋势保持，标准差相比 RUNS=5 下降 63%
- noisy_scx_normal 在 GET/INCR/SET 上 RPS 改善趋势保持
- noisy_scx_psi 在部分操作上有一定正向效果
- noisy_scx_always_active 不稳定优于其他模式

## Nginx 结果摘要

- noisy_cgroup_v2 表现更稳
- sched_ext 部分模式存在明显 P99 代价
- 详见报告中策略适配边界分析

## 关键证据链

1. latency + background 场景前提成立
2. PsiGate 进入 ACTIVE
3. cgroup_v2 存在 applied=yes / sched_ext 存在 executor=sched_ext
4. 业务结果写入正式候选目录

## 图表（7 张 SVG）

redis/nginx sched_ext rps/p99/quiet_overhead + psigate_timeline

## 可提交边界

已支撑：统一 Agent 架构、双执行后端、Redis 15 轮正式结果、Nginx 边界分析、Skills 框架、network/security demo、中文报告、图表材料

项目代码：\https://github.com/shibuchou/EulerPilot\
