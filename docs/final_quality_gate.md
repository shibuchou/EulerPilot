# EulerPilot 最终质量门禁

更新时间：`2026-06-21`

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

## P0 阻塞项（17 项）

| # | 检查 | 判定 |
|---|------|------|
| 1 | `make agent` | exit code=0 |
| 2 | `make network-policy-demo` | exit code=0 |
| 3 | `make network-qos-tc` | exit code=0 |
| 4 | `make network-xdp-demo` | exit code=0 |
| 5 | `make security-policy-demo` | exit code=0 |
| 6 | `--list-skills` | 至少输出正式 `network_policy/network_qos/network_xdp/security_policy` |
| 7 | `--doctor-skills` | exit code=0 |
| 8 | agent 15s smoke | timeout 内正常退出 |
| 9 | `network_policy` | 默认 `enabled: false` |
| 10 | `network_qos` | 默认 `enabled: false` |
| 11 | `network_xdp` | 默认 `enabled: false` |
| 12 | `security_policy` | 默认 `enabled: false` |
| 13 | `security_policy_demo` | 默认 `enabled: false` |
| 14 | metrics exporter | 默认关闭 + 监听 127.0.0.1:9108 |
| 15 | Dashboard | `reports/dashboard/index.html` 存在且非空 |
| 16 | 冻结结果目录 | Redis/Nginx 各 ≥1 个目录 |
| 17 | 无 BPF/LSM/TC/XDP 残留 | security LSM link 不存在，demo-net cgroup 无 attached BPF，lab veth 不残留 |

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

- 17 项 P0 全部 `ok`
- P1 记录结果但不影响通过
- 122 编译 + CLI 通过

## 最新记录

- 121 最新完整门禁：`reports/final_quality_gate_20260621_security_pid_target.log`
- 结果：17/17 P0 通过，P1 `agent 100-round stress smoke` 和 `doctor 5-round stable` 通过
