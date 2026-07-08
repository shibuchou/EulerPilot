# EulerPilot 最终提交指南

更新时间：`2026-07-08`

本文是最终提交和答辩前的主入口。评审如果只看一份文档，应先看本文，再按链接进入代码、实验、证据和演示材料。

## 1. 项目定位

EulerPilot 是面向 openEuler 的自适应资源管控 Agent。项目以 `SP3 + cgroup v2` 作为稳定主交付路径，以 `SP4 / OLK-6.6 + sched_ext/scx` 作为增强验证路径，通过 eBPF/PSI 感知 workload，经过 Policy Engine 与 Skills 框架做策略决策，最终由 Resource、Network、Security 等 Skill 执行可审计、可回滚的控制动作。

核心闭环：

```text
eBPF/PSI 观测
-> Workload 感知
-> Policy Engine 决策
-> Skill Manager 编排
-> Cgroup / scx / TC / XDP / LSM 执行
-> AuditBus + ActionJournal + Evidence
-> Web Console 旁路展示
```

## 2. 推荐阅读顺序

1. `README.md`：项目总览和目录结构。
2. `docs/project_brief.md`：赛题背景与作品目标。
3. `docs/architecture.md`：系统架构与模块边界。
4. `docs/final_evidence_index.md`：最终证据索引。
5. `docs/final_report_submission.md`：正式报告主稿。
6. `docs/defense_final.md`：答辩主文档。
7. `submission/README.md`：提交包入口。

## 3. 编译与基础验证

在 openEuler 主机上执行：

```bash
cd /root/EulerPilot
make agent
make unit-tests
make network-policy network-qos-tc network-xdp security-policy
./build/eulerpilot-agent --validate-config configs/agent.yaml
./build/eulerpilot-agent --list-skills
./build/eulerpilot-agent --doctor-skills --config configs/agent.yaml
```

最终质量门禁：

```bash
scripts/final_quality_gate.sh
```

当前最终门禁口径：`22/22 P0`、`100` 轮 smoke、`5` 轮 doctor 通过。

## 4. 实验复现入口

稳定主线：

```bash
tests/integration/test_resource_control.sh
tests/integration/test_resource_control_io.sh
tests/integration/test_policy_engine_security_network_resource.sh
```

Network / Security / Policy Engine：

```bash
tests/integration/test_network_qos_tc.sh
tests/integration/test_network_xdp.sh
tests/integration/test_security_policy.sh
tests/integration/test_security_policy_anomaly_rules.sh
tests/integration/test_policy_engine_security_resource.sh
tests/integration/test_policy_engine_security_network_resource.sh --repeat 10
```

最终结果汇总见：

- `reports/final_evidence_compact.md`
- `reports/final_evidence_compact.json`
- `results/final/`
- `results/policy_engine/security-network-resource-20260706-164539`

## 5. Web Console 演示

Web Console 是旁路展示控制台，不进入 Agent 热路径，不生成新的性能结论。它读取现有 CLI、日志、结果和 evidence 文件，并通过白名单动作运行少量可控演示。

121 主机启动：

```bash
cd /root/EulerPilot
web_console/scripts/run_console.sh --daemon
```

本地访问：

```bash
ssh -L 18080:127.0.0.1:18080 EulerPilot-openEuler
```

浏览器打开：

```text
http://127.0.0.1:18080
```

推荐现场只运行：

```text
环境检查 -> 查看 Skills -> Agent 状态 JSON -> Skill 诊断 -> 离线证据演示 -> 跨 Skill 联动实验 -> 清理现场资源
```

## 6. 当前同步状态

最新代码已同步到：

- 本地：`D:\code\Ubuntu\EulerPilot`
- SP4 主验证仓库：`192.168.1.123:/root/EulerPilot`
- 121/SP3 历史仓库：`192.168.1.121:/root/EulerPilot`
- 122：`192.168.1.122:/root/EulerPilot`
- GitHub：`https://github.com/shibuchou/EulerPilot`

最终提交前再次生成：

```bash
reports/final_repo_consistency_YYYYMMDD-HHMM.log
```

## 7. 不建议现场临时做的事

- 不在默认配置中开启强制 Network/Security 控制。
- 不在真实业务网卡上运行 TC/XDP 演示。
- 不在未确认环境的 Kubernetes 集群中运行 real Pod 高级演示。
- 不把 Web Console 当作新的控制平面解释；它只是 evidence-first 展示层。
