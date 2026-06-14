# EulerPilot 最终结果摘要

说明：

- 文中提到的正式候选结果目录与图表目录位于远端验证机 `192.168.1.122` 的 `/root/EulerPilot` 下。

更新时间：`2026-06-12`

## 1. 当前最强候选结果目录

当前建议直接在最终报告和答辩中引用的两个核心目录为：

- Redis：`/root/EulerPilot/results/final/redis-scx-compare-20260612-191543`
- Nginx：`/root/EulerPilot/results/final/nginx-scx-compare-20260612-194018`

说明：

- 上述目录位于远端验证机 `192.168.1.122` 的 `/root/EulerPilot` 下。

它们当前都满足：

- `RUNS=5`
- 多后端统一矩阵
- `run_manifest.json`
- 平衡轮换顺序
- 无 `invalid_run`
- 中文报告

---

## 2. Redis 结果摘要

### 2.1 正式矩阵

Redis 正式矩阵包括：

- `quiet_default`
- `quiet_scx_normal`
- `noisy_default`
- `noisy_cgroup_v2`
- `noisy_scx_normal`
- `noisy_scx_always_active`
- `noisy_scx_psi`

### 2.2 可直接复述的结论

基于 `/root/EulerPilot/results/final/redis-scx-compare-20260612-191543/compare_summary_avg.csv`：

- `noisy_cgroup_v2`
  - 在 `GET` 上相对 `noisy_default` 出现明显吞吐提升趋势
- `noisy_scx_normal`
  - 在 `GET`、`INCR`、`SET` 上出现较明显的吞吐正向趋势
  - 在 `SET` 上同时呈现较明显的 P99 改善趋势
- `noisy_scx_psi`
  - 在 `GET` 上有一定吞吐提升趋势
  - 但并不是所有操作都优于 `noisy_default`
- `noisy_scx_always_active`
  - 并不稳定优于其他模式
  - 在部分操作上存在更高尾延迟代价

### 2.3 正式表述建议

Redis 部分建议写成：

> 在 Redis + stress-ng 场景下，EulerPilot 已经形成可复现的多后端正式对照结果。实验显示，不同后端与门控模式对不同操作的影响存在差异；其中 `noisy_scx_normal` 与 `noisy_cgroup_v2` 在部分关键操作上表现出正向趋势，但 `sched_ext` 不宜被简单概括为“全面优于默认调度器”。

---

## 3. Nginx 结果摘要

### 3.1 正式矩阵

Nginx 正式矩阵与 Redis 保持一致：

- `quiet_default`
- `quiet_scx_normal`
- `noisy_default`
- `noisy_cgroup_v2`
- `noisy_scx_normal`
- `noisy_scx_always_active`
- `noisy_scx_psi`

### 3.2 可直接复述的结论

基于 `/root/EulerPilot/results/final/nginx-scx-compare-20260612-194018/compare_summary_avg.csv`：

- `noisy_cgroup_v2`
  - 相对 `noisy_default` 呈现轻微正向吞吐趋势
  - 当前 P99 未出现恶化
- `noisy_scx_psi`
  - 吞吐接近 `noisy_default`
  - 但当前 P99 仍偏高
- `noisy_scx_always_active`
  - 当前 P99 代价明显
- `quiet_scx_normal`
  - 当前存在显著基础开销，说明 `sched_ext` 常驻开销在 Nginx 场景下不能忽略

### 3.3 正式表述建议

Nginx 部分建议写成：

> 在 Nginx + stress-ng 场景下，EulerPilot 已经验证了 `sched_ext` 正式 compare 框架可迁移到第二业务线。当前结果表明，Nginx 对不同 `sched_ext` 模式的敏感性与 Redis 存在差异，`cgroup_v2` 在该场景下表现更稳，而某些 `sched_ext` 模式仍存在明显尾延迟代价。

---

## 4. 关键证据链

当前可以直接引用的关键证据包括：

### 4.1 PsiGate

- Redis `run-1/noisy_scx_psi_psi_gate_trace.jsonl`
  - 存在 `NORMAL -> ARMED -> ACTIVE`
- Nginx `run-1/noisy_scx_psi_psi_gate_trace.jsonl`
  - 存在 `NORMAL -> ARMED -> ACTIVE`

### 4.2 cgroup v2 执行动作

- Redis `run-1/noisy_cgroup_v2_agent_snapshot.txt`
  - 存在 `applied=yes reason=assigned`
- Nginx `run-1/noisy_cgroup_v2_agent_snapshot.txt`
  - 存在 `applied=yes reason=assigned`

### 4.3 sched_ext 执行动作

- Redis `run-1/noisy_scx_psi_agent_snapshot.txt`
  - 存在 `executor=sched_ext`
- Nginx `run-1/noisy_scx_psi_agent_snapshot.txt`
  - 存在 `executor=sched_ext`

### 4.4 图表材料

已生成：

- `/root/EulerPilot/reports/final_figures/redis_sched_ext_rps.svg`
- `/root/EulerPilot/reports/final_figures/redis_sched_ext_p99.svg`
- `/root/EulerPilot/reports/final_figures/nginx_sched_ext_rps.svg`
- `/root/EulerPilot/reports/final_figures/nginx_sched_ext_p99.svg`
- `/root/EulerPilot/reports/final_figures/redis_quiet_overhead.svg`
- `/root/EulerPilot/reports/final_figures/nginx_quiet_overhead.svg`
- `/root/EulerPilot/reports/final_figures/psigate_timeline.svg`

---

## 5. 当前可提交边界

当前已经可以支撑以下提交内容：

- 统一 Agent 架构
- 双执行后端
- Redis 正式结果
- Nginx 正式结果
- 中文实验说明
- 中文阶段汇总
- 中文最终报告草稿
- 图表材料

当前尚不建议写成：

- `sched_ext` 已全面优于默认调度器
- 所有场景都取得稳定收益

更稳的最终结论应写成：

> EulerPilot 已经完成从主闭环到双后端正式 compare 的工程收口，并形成了 Redis 与 Nginx 两条业务线的多轮候选结果目录；当前结果能够支撑系统创新与工程实现两方面的展示，但 `sched_ext` 在不同 workload 上的收益与代价仍需按具体场景谨慎解释。
