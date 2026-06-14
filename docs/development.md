# 开发说明

## 工作位置

默认在 openEuler 服务器上开发：

```bash
cd /root/EulerPilot
```

本地 `D:\code\Ubuntu\EulerPilot` 仅作为同步镜像，未经用户要求不主动同步。

## 构建

```bash
make
```

如需只构建 eBPF observer：

```bash
make observer
```

## 运行

主 Agent 运行入口：

```bash
./scripts/run_agent.sh
```

eBPF observer 第一版可单独验证：

```bash
timeout 5s ./build/workload_observer_dump
```

当前 Agent 已能：

- 加载 observer
- 读取真实 task 指标
- 读取 `/proc/pressure/cpu|memory|io` 的 PSI 数据
- 完成第一版 workload 分类
- 将分类结果接到 `cgroup v2` 执行链路
- 按 profile 调整 `cpu.weight`

当前 PSI 状态：

- 第一阶段采用用户态 `psi_reader`
- 已接入 Agent 主循环
- 当前先使用静态阈值，后续再通过多轮实验调成更合适的值，并逐步演进到 baseline 自适应阈值

`cpuset` 已进入执行设计，但当前主实验仍以 `cpu.weight + cgroup.procs` 为稳定路径。`sched_ext` 侧已经接入第一版 `ScxExecutor + scx_eulerpilot` 原型，但当前仍处于 smoke 和参数收敛阶段，尚未替代主实验默认后端。

当前实验边界：

- `redis-server` 视为被保护服务
- `stress-ng` 视为后台干扰 workload
- `redis-benchmark` 只作为压测客户端，默认留在根组
- `cpuset` 不作为当前主实验必要条件

## 实验框架验证入口

```bash
FRONT_CMD='sleep 3' BACK_CMD='yes >/dev/null' INTERVAL_MS=1000 \
  ./bench/mixed/run_placeholder_benchmark.sh
```

## Redis 正式实验入口

```bash
./bench/redis/run_redis_stress_benchmark.sh
```

如需切到 `sched_ext` 后端，可通过环境变量统一切换：

```bash
BACKEND=sched_ext ACTIVE_PREFIX=active_noisy_sched_ext ./bench/redis/run_redis_main_experiment.sh
```

该脚本会生成：

- 多轮 `baseline/default_noisy/active_noisy` 原始数据
- `compare_summary_avg.csv`
- 中文 Markdown 报告 `report.md`
- Agent 快照
- 系统快照

## 代码风格

- 主要语言：C / C++
- C++ 用户态代码默认 C++17
- eBPF 程序使用 C
- 公共结构放在 `agent/include/` 或模块内头文件
- 系统级改动必须保留回滚路径
