# EulerPilot 最终质量门禁

更新时间：`2026-07-06`

## 目的

在代码冻结后、最终提交前，验证项目的构建、运行、安全默认值和残留清理是否达标。

## 运行命令

```bash
# 121 全部门禁
./scripts/final_quality_gate.sh | tee reports/final_quality_gate_121.tap

# 122 最小门禁
cd /root/EulerPilot
make agent
./build/eulerpilot-agent --list-skills
./build/eulerpilot-agent --doctor-skills --config configs/agent.yaml
```

## P0 阻塞项（22 项）

| # | 检查 | 判定 |
|---|------|------|
| 1 | `make agent` | exit code=0 |
| 2 | `make network-policy` | exit code=0 |
| 3 | `make network-qos-tc` | exit code=0 |
| 4 | `make network-xdp` | exit code=0 |
| 5 | `make security-policy` | exit code=0 |
| 6 | `make unit-tests` | C++ 用户态单元测试 exit code=0 |
| 7 | `--list-skills` | 至少输出正式 `network_policy/network_qos/network_xdp/security_policy` |
| 8 | `--doctor-skills` | exit code=0 |
| 9 | agent 15s smoke | timeout 内正常退出 |
| 10 | `network_policy` | 默认 `enabled: false` |
| 11 | `network_qos` | 默认 `enabled: false` |
| 12 | `network_xdp` | 默认 `enabled: false` |
| 13 | `security_policy` | 默认 `enabled: false` |
| 14 | `security_policy_demo` | 兼容入口默认 `enabled: false` |
| 15 | metrics exporter | 默认关闭 + 监听 127.0.0.1:9108 |
| 16 | Dashboard | `reports/dashboard/index.html` 存在且非空 |
| 17 | 冻结结果目录 | Redis/Nginx 各 ≥1 个目录 |
| 18 | Resource Control CPU+Memory+IO 证据 | 121/122 CPU+Memory 与 IO 结果目录存在，`summary.txt` 为 pass，包含 `cpu.max`、`memory.high`、`io.max`、`io.weight` 关键证据 |
| 19 | Resource Control target_ref 证据 | 121/122 target 结果目录存在，`summary.txt` 为 pass，包含 `target_ref`、目标 cgroup、非目标 cgroup 未误改和 `cpu.max/memory.high` 关键证据 |
| 20 | Resource Control runtime target 证据 | 121/122 runtime target 结果目录存在，`summary.txt` 为 pass，包含 `container_id/container/k8s_pod`、三类 `target_ref` 和 `cpu.max/memory.high` 关键证据 |
| 21 | Resource Control CPU quota 效果证据 | 121/122 CPU quota 结果目录存在，`summary.txt` 为 pass，`usage_rate_ratio < 0.70`，且 `nr_throttled/throttled_usec` 增长 |
| 22 | 无 BPF/LSM/TC/XDP 残留 | security LSM link 不存在，demo-net cgroup 无 attached BPF，lab veth 不残留 |

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

## 122 最小门禁

```bash
cd /root/EulerPilot
make agent
./build/eulerpilot-agent --list-skills
./build/eulerpilot-agent --doctor-skills --config configs/agent.yaml
```

## 通过标准

- 22 项 P0 全部 `ok`
- P1 记录结果但不影响通过
- 122 编译 + CLI 通过

## 最新记录

- 121 最新完整门禁：`reports/final_quality_gate_20260706-quality-121.log`
- 结果：22/22 P0 通过，P1 `agent 100-round stress smoke` 和 `doctor 5-round stable` 通过。
