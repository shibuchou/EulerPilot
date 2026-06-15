# EulerPilot 最终质量门禁

更新时间：`2026-06-15`

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

## P0 阻塞项（12 项）

| # | 检查 | 判定 |
|---|------|------|
| 1 | `make agent` | exit code=0 |
| 2 | `make network-policy-demo` | exit code=0 |
| 3 | `make security-policy-demo` | exit code=0 |
| 4 | `--list-skills` | 输出 4 项 |
| 5 | `--doctor-skills` | exit code=0 |
| 6 | agent 15s smoke | timeout 内正常退出 |
| 7 | network_policy_demo | 默认 `enabled: false` |
| 8 | security_policy_demo | 默认 `enabled: false` |
| 9 | metrics exporter | 默认关闭 + 监听 127.0.0.1:9108 |
| 10 | Dashboard | `reports/dashboard/index.html` 存在且非空 |
| 11 | 冻结结果目录 | Redis/Nginx 各 ≥1 个目录 |
| 12 | 无 BPF/LSM 残留 | security LSM link 不存在，demo-net cgroup 无 attached BPF |

## P1 可选项（不阻塞）

| # | 检查 |
|---|------|
| 13 | agent 连续 10 轮短 smoke |
| 14 | doctor 连续 5 轮 probe 稳定 |
| 15 | metrics 临时启用验证 |

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

- 12 项 P0 全部 `ok`
- P1 记录结果但不影响通过
- 122 编译 + CLI 通过
