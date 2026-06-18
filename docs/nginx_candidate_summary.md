# Nginx 第二实验线状态摘要

## 当前定位

Nginx 实验线用于证明当前方案不是 Redis 特化，而是对通用前台服务 + 后台干扰场景都具备一定的资源管控能力。

当前入口：

```bash
./bench/nginx/run_nginx_main_experiment.sh
```

## 当前默认参数

与当前 Redis 主候选保持一致：

```text
latency_weight = 1000
background_weight = 20

cpu_psi_threshold = 0.05
latency_wait_threshold_ns = 500000
background_runtime_threshold_ns = 2500000
```

Nginx 压测参数：

```text
wrk_threads = 2
wrk_connections = 32
wrk_duration = 10s
stress_workers = 2
```

## 当前已完成内容

- 已安装 `nginx`
- 已安装 `wrk`
- 已实现第一版实验脚本：
  - `baseline`
  - `default_noisy`
  - `active_noisy`
- 已实现 wrk 输出提取
- 已实现中文 Markdown 报告生成

## 当前边界

- `nginx` 作为被保护服务
- `wrk` 仅作为压测客户端，不参与分类控制
- `stress-ng` 作为后台干扰 workload

## 下一步

1. 用正式入口跑一轮中等规模 Nginx 主实验
2. 检查 Agent 是否能正确识别 `nginx`
3. 比较 `default_noisy` 与 `active_noisy` 的吞吐和尾延迟趋势

## 当前首轮正式候选结果

当前首轮正式候选结果目录：

```text
/root/EulerPilot/results/reports/nginx-20260605-110152
```

当前结果显示：

- `active_noisy` 相对 `default_noisy`
  - 吞吐提升约 `22.42%`
  - P99 从 `2.06ms` 改善到 `1.63ms`
- Agent 已经能够在压测进行中同时捕获：
  - `nginx` 作为延迟敏感 workload
  - `stress-ng` 作为后台干扰 workload
  - `applied=yes` 的控制证据

这说明当前第二实验线已经不仅仅是脚本可运行，而是已经具备“初步证明方案不是 Redis 特化”的能力。
