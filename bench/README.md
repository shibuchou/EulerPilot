# Benchmark 套件说明

实验脚本按 workload 分目录组织：

- `redis/`：Redis + stress-ng 抗干扰实验
- `nginx/`：Nginx + wrk 抗干扰实验
- `sysbench/`：吞吐型 workload 实验
- `mixed/`：混合 workload 自适应切换实验

所有实验输出统一落到 `results/` 下，包含：

- CSV 原始数据
- 汇总结果
- Markdown 报告
- 正式 compare 目录
- 候选结果索引

## 当前阶段

当前实验脚本已经不再停留在 smoke 或单轮试跑，而是已经具备正式 compare 能力。

### Redis

- `bench/redis/run_redis_stress_benchmark.sh`
- `bench/redis/run_redis_main_experiment.sh`
- `bench/redis/run_redis_final_experiment.sh`
- `bench/redis/run_redis_sched_ext_compare.sh`
- `bench/redis/run_redis_sched_ext_psi_probe.sh`

当前最强候选结果目录：

- `/root/EulerPilot/results/final/redis-scx-compare-20260612-191543`
- SP4 sched_ext 内核第一轮：`/root/EulerPilot/results/final/redis-scx-compare-20260706-101505`
- SP4 PSI ACTIVE probe：`/root/EulerPilot/results/final/redis-scx-psi-probe-20260706-100857`

`run_redis_sched_ext_psi_probe.sh` 专门用于验证 PSI gate 的
`NORMAL -> ARMED -> ACTIVE` 状态迁移，避免短性能对照窗口与门控触发证据互相干扰。

### Nginx

- `bench/nginx/run_nginx_main_experiment.sh`
- `bench/nginx/run_nginx_sched_ext_compare.sh`

当前最强候选结果目录：

- `/root/EulerPilot/results/final/nginx-scx-compare-20260612-194018`
- SP4 sched_ext 内核第一轮：`/root/EulerPilot/results/final/nginx-scx-compare-20260706-101928`

### 图表与结果材料

当前最终图表位于：

- `/root/EulerPilot/reports/final_figures`

当前中文总结材料位于：

- `/root/EulerPilot/docs/stage_delivery_summary.md`
- `/root/EulerPilot/docs/final_results_summary.md`
- `/root/EulerPilot/docs/final_report_v2.md`

### Skills / YAML 下一步主入口

- `configs/agent.yaml`
- `configs/skills.yaml`
- `docs/skills_yaml_plan.md`
- `make network-policy-demo`
