# tests/benchmark

作用：存放会输出可量化指标的阶段性 Benchmark。这里的脚本用于报告和答辩证据，不默认进入 P0 质量门禁。

## 当前测试

- `test_network_qos_rate.sh`：在 isolated netns/veth 上验证 `network_qos` TC egress + TBF 的限速效果，输出 baseline/enforce 吞吐、目标速率、误差和限速倍数。

## 维护规则

- Benchmark 必须写入独立 `results/` 子目录。
- 会修改网络、cgroup、TC、XDP、LSM 状态的脚本必须自带 cleanup。
- Benchmark 结论必须写明目标、环境、命令和误差边界，不能只写 PASS。
