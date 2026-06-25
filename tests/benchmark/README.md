# tests/benchmark

作用：存放会输出可量化指标的阶段性 Benchmark。这里的脚本用于报告和答辩证据，不默认进入 P0 质量门禁。

## 当前测试

- `test_network_qos_rate.sh`：在 isolated netns/veth 上验证 `network_qos` TC egress + TBF 的限速效果，输出 baseline/enforce 吞吐、目标速率、误差和限速倍数。
- `test_resource_control_redis_quota.sh`：二阶段历史基准，对比默认 noisy 与 EulerPilot `cpu.max=10000 100000` quota 下的 Redis RPS、background cgroup `usage_usec`、`nr_throttled` 和 `throttled_usec`。该脚本保留用于回归，不作为当前首选报告口径。
- `test_resource_control_redis_quota_compare.sh`：当前推荐 Redis quota 证据。它拆分 `default_noisy`、`eulerpilot_no_quota`、`eulerpilot_quota` 三个阶段，避免把 Agent 放置影响和 quota 影响混在一起；通过线聚焦同样 Agent 放置下 background CPU 使用率下降和 throttling 增长，Redis GET/SET RPS 只作为业务侧边界指标。121 最新通过结果：`results/resource_control/redis-quota-compare-20260625-102426`；122 最新通过结果：`results/resource_control/redis-quota-compare-20260625-102611`。

## 维护规则

- Benchmark 必须写入独立 `results/` 子目录。
- 会修改网络、cgroup、TC、XDP、LSM 状态的脚本必须自带 cleanup。
- Benchmark 结论必须写明目标、环境、命令和误差边界，不能只写 PASS。
