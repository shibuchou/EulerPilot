# EulerPilot 最终质量门禁

更新时间：`2026-07-26`

## 目的

在代码冻结后、最终提交前，验证项目的构建、运行、安全默认值、残留清理、evidence 状态和 benchmark 语义是否达标。v6 后门禁分为 preflight、candidate-bound gate、formal artifact gate 和 release gate。

## 运行命令

```bash
# SP4 v6 收口仓库 preflight
./scripts/final_quality_gate.sh | tee reports/final_quality_gate_sp4.tap

# 122 / OLK 对照线最小门禁
cd /root/EulerPilot
make agent
./build/eulerpilot-agent --list-skills
./build/eulerpilot-agent --doctor-skills --config configs/agent.yaml
```

## P0 阻塞项（当前 v6 preflight 29 项）

| # | 检查 | 判定 |
|---|------|------|
| 1 | `make agent` | exit code=0 |
| 2 | `make network-policy` | exit code=0 |
| 3 | `make network-qos-tc` | exit code=0 |
| 4 | `make network-xdp` | exit code=0 |
| 5 | `make security-policy` | exit code=0 |
| 6 | `make unit-tests` | C++ 用户态单元测试 exit code=0 |
| 7 | 配置 schema / 未消费字段检查 | `tests/integration/test_config_validation.sh` 通过，拒绝未知字段和未消费历史字段 |
| 8 | Benchmark release 语义检查 | `tests/integration/test_benchmark_release_semantics.sh` 通过，确认 default baseline 与 randomized block 语义 |
| 9 | PolicyEngine 事务模型 | `tests/integration/test_policy_engine_transaction_model.sh` 通过，确认每次请求使用独立事务上下文 |
| 10 | ResourceControl rollback 模型 | `tests/integration/test_resource_control_rollback_model.sh` 通过，确认 verify-fail rollback、snapshot 保留和不可逆动作隔离 |
| 11 | SCX loader ownership | `tests/integration/test_scx_loader_ownership.sh` 通过，确认不使用 `pkill` 误杀外部实例 |
| 12 | SecurityPolicy fail-closed | `tests/integration/test_security_policy_fail_closed.sh` 通过，确认 enforce 必须 scoped、socket 协议不可用时拒绝 |
| 13 | Evidence validate-run fixtures | `tests/integration/test_evidence_validation_fixtures.sh` 通过，确认 throughput/mixed 负向样例不能误判 pass |
| 14 | `--list-skills` | 至少输出正式 `network_policy/network_qos/network_xdp/security_policy` |
| 15 | `--doctor-safe` | exit code=0，不做 live probe |
| 16 | agent 15s smoke | timeout 内正常退出 |
| 17 | `network_policy` | 默认 `enabled: false` |
| 18 | `network_qos` | 默认 `enabled: false` |
| 19 | `network_xdp` | 默认 `enabled: false` |
| 20 | `security_policy` | 默认 `enabled: false` |
| 21 | `security_policy_demo` | 兼容入口默认 `enabled: false` |
| 22 | metrics exporter | 默认关闭 + 监听 127.0.0.1:9108 |
| 23 | Dashboard | `reports/dashboard/index.html` 存在且非空 |
| 24 | 冻结结果目录 | Redis/Nginx 各 ≥1 个目录 |
| 25 | Resource Control CPU+Memory+IO 证据 | 121/122 CPU+Memory 与 IO 结果目录存在，`summary.txt` 为 pass，包含 `cpu.max`、`memory.high`、`io.max`、`io.weight` 关键证据 |
| 26 | Resource Control target_ref 证据 | 121/122 target 结果目录存在，`summary.txt` 为 pass，包含 `target_ref`、目标 cgroup、非目标 cgroup 未误改和 `cpu.max/memory.high` 关键证据 |
| 27 | Resource Control runtime target 证据 | 121/122 runtime target 结果目录存在，`summary.txt` 为 pass，包含 `container_id/container/k8s_pod`、三类 `target_ref` 和 `cpu.max/memory.high` 关键证据 |
| 28 | Resource Control CPU quota 效果证据 | 121/122 CPU quota 结果目录存在，`summary.txt` 为 pass，`usage_rate_ratio < 0.70`，且 `nr_throttled/throttled_usec` 增长 |
| 29 | 无 BPF/LSM/TC/XDP 残留 | security LSM link 不存在，demo-net cgroup 无 attached BPF，lab veth 不残留 |

## P1 可选项（不阻塞）

| # | 检查 |
|---|------|
| P1-1 | agent 连续 100 轮短 smoke |
| P1-2 | doctor 连续 5 轮 probe 稳定 |
| P1-3 | metrics 临时启用验证 |

## 已知可接受项

- 编译 warning 记录但不阻塞 P0
- Valgrind 未安装，跳过内存泄漏检查
- 121 上无 git 配置
- 第三方库 `still reachable` 不视为泄漏

## 122 / OLK 对照线最小门禁

```bash
cd /root/EulerPilot
make agent
./build/eulerpilot-agent --list-skills
./build/eulerpilot-agent --doctor-skills --config configs/agent.yaml
```

## 通过标准

- 29 项 P0 全部 `ok`
- P1 记录结果但不影响通过
- 122 编译 + CLI 通过

## 最新记录

- SP4 历史完整门禁：`reports/final_quality_gate_20260720-stage3-performance.log`，结果为 22/22 P0、100 轮 smoke、5 轮 doctor，通过但仅作为历史记录。
- v6 当前状态：`/root/EulerPilot-closeout` 缩短版 preflight 29/29 P0 通过。
- 最终 release gate：必须在同一 `tested_candidate_commit`、formal `artifact_id` 和修正 baseline 后正式实验上重新运行。
- 121/SP3 与 122/OLK 既有门禁和回归记录保留为历史对照，不再作为最终主验证线。
