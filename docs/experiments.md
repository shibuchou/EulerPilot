# 实验设计

> 当前最终实验口径已更新到 `2026-07-08`：SP4 主验证线完成 Redis/Nginx `RUNS=5` 复核、Redis pressure gradient 和 Redis manual static vs agent dynamic 对比。本文中早期 `RUNS=1/3` 内容保留为阶段实验记录；最终提交与答辩请以 `docs/final_evidence_index.md`、`docs/final_report_submission.md` 和 `reports/final_evidence_compact.md` 为准。

## Redis 抗干扰实验

工作负载：

- 前台：Redis + redis-benchmark
- 后台：stress-ng --cpu N

对比对象：

1. openEuler 默认调度器
2. EulerPilot `default_noisy`
3. EulerPilot `active_noisy`

当前主交付仍以 `active_noisy` 对 `default_noisy` 的差异为主验证目标。

同时，`192.168.1.122` 上的 `sched_ext` 线已经完成：

- `loader-only wiring` smoke
- `gate_mode=normal` 回归
- `gate_mode=always-active` 回归
- `PsiGate v1` 的 `redis_repeat3` 闭环 smoke

因此后续 Redis / Nginx 正式对照将采用以下 `sched_ext` 模式：

- `sched_ext normal`
- `sched_ext always-active`
- `sched_ext psi`

其中 Redis `sched_ext` 正式对照入口已经补齐为：

```bash
./bench/redis/run_redis_sched_ext_compare.sh
```

当前这条入口已经在 `192.168.1.122` 上完成：

- `RUNS=1` 首轮小规模正式验证
- `RUNS=3` 平衡轮换正式候选验证
- `RUNS=5` 正文候选长跑验证

固定矩阵为：

- `quiet_default`
- `quiet_scx_normal`
- `noisy_default`
- `noisy_cgroup_v2`
- `noisy_scx_normal`
- `noisy_scx_always_active`
- `noisy_scx_psi`

当前结果目录示例：

- `/root/EulerPilot/results/final/redis-scx-compare-20260612-185007`
- `/root/EulerPilot/results/final/redis-scx-compare-20260612-185727`
- `/root/EulerPilot/results/final/redis-scx-compare-20260612-191543`

该目录已经包含：

- `run_manifest.json`
- `compare_summary_avg.csv`
- `report.md`
- `summary.md`
- `run-1/<label>_summary.csv`
- `run-1/<label>_agent_snapshot.txt`
- `run-1/<label>_scx_stats.json`
- `run-1/<label>_gate_status.txt`
- `run-1/<label>_psi_gate_trace.jsonl`

其中 `20260612-185727` 这一轮已经进一步满足：

- `RUNS=3`
- 平衡轮换顺序已写入 `run_manifest.json`
- 当前无 `invalid_run`
- 每轮 `noisy_scx_psi` 都存在 `ACTIVE` 命中

其中 `20260612-191543` 这一轮已经进一步满足：

- `RUNS=5`
- 平衡轮换顺序已写入 `run_manifest.json`
- 当前无 `invalid_run`
- 五轮 `noisy_scx_psi` 都存在 `gate_state=2`
- 已形成更接近正文候选的 `compare_summary_avg.csv` 与中文报告

实验边界说明：

- `redis-server` 进入 `/eulerpilot/latency`
- `stress-ng` 进入 `/eulerpilot/background`
- `redis-benchmark` 保持在默认根组，只作为压测客户端，不参与分类控制
- `cpu.weight` 的解释作用域限定在同一父级 `/eulerpilot` 下的 sibling cgroup 之间

指标：

- RPS
- average latency
- P95/P99/P999 latency
- CPU PSI
- runqueue wait time
- context switch count
- migration count

## Nginx 抗干扰实验

工作负载：Nginx + wrk + stress-ng/make -j

目标：证明方案不是 Redis 特化。

当前入口：

```bash
./bench/nginx/run_nginx_main_experiment.sh
```

第一版输出与 Redis 主实验保持一致，包含：

- baseline / default_noisy / active_noisy
- wrk 原始输出
- wrk 摘要 CSV
- Agent 快照
- 中文 Markdown 报告

同时，Nginx 的 `sched_ext` 正式对照入口已经补齐为：

```bash
./bench/nginx/run_nginx_sched_ext_compare.sh
```

当前已在 `192.168.1.122` 上完成 `RUNS=1` 最小正式验证，结果目录为：

- `/root/EulerPilot/results/final/nginx-scx-compare-20260612-191150`
- `/root/EulerPilot/results/final/nginx-scx-compare-20260612-193017`
- `/root/EulerPilot/results/final/nginx-scx-compare-20260612-194018`

该目录已经包含：

- `run_manifest.json`
- `compare_summary_avg.csv`
- `report.md`
- `summary.md`
- `run-1/<label>_summary.csv`
- `run-1/<label>_agent_snapshot.txt`
- `run-1/<label>_scx_stats.json`
- `run-1/<label>_gate_status.txt`

并且当前已验证到：

- `noisy_scx_psi` 存在 `ACTIVE` 命中
- `noisy_cgroup_v2` 存在 `applied=yes reason=assigned`
- `noisy_scx_psi` 存在 `executor=sched_ext` 运行证据

其中 `20260612-193017` 这一轮已经进一步满足：

- `RUNS=3`
- 平衡轮换顺序已写入 `run_manifest.json`
- 当前无 `invalid_run`
- 每轮 `noisy_scx_psi` 都存在 `gate_state=2`

其中 `20260612-194018` 这一轮已经进一步满足：

- `RUNS=5`
- 平衡轮换顺序已写入 `run_manifest.json`
- 当前无 `invalid_run`
- 五轮 `noisy_scx_psi` 都存在 `gate_state=2`
- 已形成更接近正文候选的 `compare_summary_avg.csv` 与中文报告

## 混合负载自适应实验

时间线：

```text
0-60s: Redis only
60-120s: Redis + stress-ng
120-180s: Redis + stress-ng + make -j
180-240s: Redis + make -j
240-300s: Redis only
```

输出 profile 切换、P99 曲线、CPU pressure 曲线和 background 控制强度。

## 当前实验框架状态

### 占位框架验证入口

```bash
FRONT_CMD='sleep 3' BACK_CMD='yes >/dev/null' INTERVAL_MS=1000 \
  ./bench/mixed/run_placeholder_benchmark.sh
```

该入口用于：

- 验证实验脚本框架本身是否可运行
- 采集系统快照
- 保存 Agent 分类输出
- 保存 `cgroup v2` 层级和 CPU PSI 快照

### Redis 正式实验入口

```bash
./bench/redis/run_redis_main_experiment.sh
```

该脚本会：

- 启动临时 Redis 实例
- 运行多轮 `baseline`
- 运行多轮 `default_noisy`
- 运行多轮 `active_noisy`
- 自动提取 `PING_INLINE`、`GET`、`SET`、`INCR` 的汇总指标
- 生成平均对比结果 `compare_summary_avg.csv`
- 生成中文 Markdown 报告 `report.md`

默认用于当前迭代阶段，建议参数为：

```text
RUNS = 5
BENCH_CLIENTS = 16
BENCH_REQUESTS = 20000
STRESS_WORKERS = 2
```

### Redis 最终长跑验证入口

```bash
./bench/redis/run_redis_final_experiment.sh
```

该脚本用于阶段末尾或正文候选结果确认，默认参数更重：

```text
RUNS = 10
BENCH_CLIENTS = 16
BENCH_REQUESTS = 100000
STRESS_WORKERS = 2
```

对于 `sched_ext` 正式对照，Redis 当前已经完成：

1. `RUNS=1~3` 的快速复验
2. `RUNS=5` 的正式正文候选验证

### Redis 参数扫描入口

```bash
./bench/redis/run_profile_sweep.sh
```

该脚本会批量尝试多组 `latency/background cpu.weight` 参数，并为每组生成独立结果目录，便于比较哪组策略更合适。

当前阶段扫描结论：

```text
latency cpu.weight = 1000
background cpu.weight = 20
```

该组参数目前是 Redis 主实验的优先候选配置。

### PSI 阈值扫描入口

```bash
./bench/redis/run_psi_threshold_sweep.sh
```

该脚本固定当前推荐的 `cpu.weight` 配置，批量扫描 `EULERPILOT_CPU_PSI_THRESHOLD`，用于观察哪一组阈值更容易形成稳定且合理的控制触发。

### 触发条件联合扫描入口

```bash
./bench/redis/run_trigger_sweep.sh
```

该脚本固定当前推荐的 `cpu.weight` 配置，同时扫描：

- `EULERPILOT_CPU_PSI_THRESHOLD`
- `EULERPILOT_LATENCY_WAIT_THRESHOLD_NS`
- `EULERPILOT_BACKGROUND_RUNTIME_THRESHOLD_NS`

用于观察哪组触发条件更容易形成稳定的轻控 / 强控分级效果。

### 局部 trigger 精扫入口

```bash
./bench/redis/run_trigger_sweep_local_refine.sh
```

该脚本围绕当前候选中心点做更小范围的 trigger 参数精扫，用于在不扩大搜索空间的前提下，继续寻找更稳的触发区间。

### INCR 定向 trigger 微调入口

```bash
./bench/redis/run_trigger_sweep_incr_refine.sh
```

该脚本固定 `cpu_psi_threshold`，围绕当前候选中心点进一步微调：

- `latency_wait_threshold_ns`
- `background_runtime_threshold_ns`

目标是重点修复 `INCR` 在正式候选实验中的退化问题。

### background_weight 微调入口

```bash
./bench/redis/run_background_weight_refine.sh
```

该脚本固定当前候选 trigger 参数，只微调：

- `background_weight = 5 / 10 / 20`

用于观察在不改变 trigger 逻辑的前提下，是否可以把 `INCR` 的退化进一步拉回，同时尽量保持 `GET/SET` 的正向趋势。

当前阶段观察：

- 在 `0.01 / 0.03 / 0.05 / 0.10` 这几组阈值下，Redis + stress-ng 小规模实验中 `cpu_psi_high` 仍大多未被打亮。
- 说明当前负载强度下，`cpu.some.avg10` 仍偏低。
- 现阶段可以先继续用 `background_runtime_high + latency_wait_high` 驱动控制，同时在后续更重负载或更小阈值下继续调试 PSI 触发。

当前阶段三参数 trigger sweep 候选结果：

```text
cpu_psi_threshold = 0.05
latency_wait_threshold_ns = 500000
background_runtime_threshold_ns = 2500000
```

该组参数在当前小规模扫描中表现相对平衡，已作为下一轮正式 Redis 主实验的候选触发参数。

## 当前实验输出

一次 Redis 正式实验至少会产出：

- `compare_summary_avg.csv`
- `report.md`
- `run-*/compare_summary.csv`
- `run-*/active_noisy_agent_snapshot.txt`
- 前后系统快照

对于 `sched_ext` 正式对照，额外需要保留：

- `run_manifest.json`
- `gate_state_map` 对应状态快照
- `scx_stats delta`
- `psi_gate_trace.jsonl`
- `sched_ext state / enable_seq / nr_rejected`
