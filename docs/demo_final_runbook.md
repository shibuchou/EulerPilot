# EulerPilot 最终演示 Runbook

更新时间：`2026-07-24`

本文用于现场答辩和本地复现。演示脚本入口为 `demo/demo_all_final.sh`，支持 live、offline 和 cleanup 三种模式。

## 模式

```bash
sudo demo/demo_all_final.sh --mode live
demo/demo_all_final.sh --mode offline
sudo demo/demo_all_final.sh --cleanup
```

- `live`：真实运行环境检查、配置校验、doctor、Policy Engine 跨 Skill 联动测试和 rollback 验证。
- `offline`：不改系统状态，只展示仓库内已保存的结果目录、图表、summary、compact evidence 和 report 位置。
- `cleanup`：清理 lab cgroup、lab veth、qdisc、临时事件文件和 demo 进程。

## live 演示顺序

```text
check_env
-> list-skills
-> validate-config configs/agent.yaml
-> validate-config configs/policy_engine_security_network_resource.yaml
-> doctor-skills configs/agent.yaml
-> status --json
-> CPU sched_ext 结果展示
-> Network QoS/XDP 结果展示
-> Security anomaly/LSM 结果展示
-> Resource CPU/Memory/IO 结果展示
-> Policy Engine cross-skill v1/v2
-> rollback/status
```

v3.1 live 核心链路：

```text
security_policy burst_connect anomaly
  -> policy_engine
  -> resource_control demo_cgroup cpu.max/memory.high
  -> network_qos ep-veth-pe0 tc/tbf 2mbit
  -> transaction_id 串联事件
  -> Agent stop rollback
```

SP4 增强验证链路已经完成，现场默认作为证据展示，不作为必须 live 重跑项。注意：SP4 发行环境已完成适配验证，但发行内核未默认启用 `CONFIG_SCHED_CLASS_EXT`；sched_ext/scx 路径基于 SP4 官方源码自编译启用内核完成复核。

```text
SP4 / 123 sched_ext kernel
  -> Redis RUNS=10 frozen-code sched_ext compare
  -> Nginx RUNS=10 frozen-code sched_ext compare
  -> final_quality_gate.sh 22/22 P0 + 100 smoke + 5 doctor
  -> final evidence compact 41 entries / missing 0 / warnings 0
```

## 演示视频状态

当前仓库未包含正式演示视频文件，不能在最终报告中写成“视频已录制”。录制脚本和命令清单见 `docs/demo_video_recording_script.md`。提交前需要按该脚本录制 8-10 分钟视频，并在提交清单中补充视频文件路径或公开访问链接。

## offline 演示材料

优先展示以下文件：

- `docs/final_evidence_index.md`
- `reports/final_evidence_compact.md`
- `docs/final_results_summary.md`
- `docs/policy_engine_skill.md`
- `docs/network_policy_skill.md`
- `docs/security_policy_skill.md`
- `docs/resource_control_skill.md`
- `docs/sp4_validation_plan.md`
- `reports/sp4/final_quality_gate_scx_workload_20260706-1214.log`
- `results/final/redis-scx-compare-20260724-tested-2541464-runs10/report.md`
- `results/final/nginx-scx-compare-20260724-tested-2541464-runs10/report.md`
- `results/policy_engine/security-network-resource-*/report.md`
- `results/policy_engine/security-network-resource-*/summary.txt`

## 现场注意事项

- live 模式需要 root 权限，因为会创建 cgroup、veth/netns 并写入 qdisc。
- `network_qos` 只允许操作 `ep-*`、`eulerpilot-*`、`lab-*` 前缀的 lab netdev；演示脚本创建 `ep-veth-pe0 <-> ep-veth-pe1`，不操作生产网卡。
- 如果现场环境缺少 `iperf3`，测试脚本会使用 Python TCP rate probe 兜底。
- 现场主演示仍建议优先跑 lab 版 Policy Engine 跨 Skill 联动；SP4 sched_ext 多轮结果、真实 Kubernetes Pod target、Pod host veth QoS/XDP 和真实 Pod Policy Engine 联动以证据展示为主，环境确认后再选择性 live 重跑。
- Web Console 建议通过 SSH 隧道访问 `127.0.0.1:18080`，先展示 Overview / Evidence / Policy Engine Timeline，再进入白名单 Demo。

## Metrics Exporter 现场启用

Prometheus `/metrics` 默认关闭，避免普通启动时额外暴露端口。现场如需展示 metrics，只使用专用配置：

```bash
./build/eulerpilot-agent --config configs/agent-metrics.yaml --duration-s 30 --interval-ms 1000 &
curl http://127.0.0.1:9108/metrics
```

边界要求：

- 只监听 `127.0.0.1:9108`，不绑定公网或生产网卡。
- 演示结束后停止 Agent，metrics exporter 会随进程退出。
- 默认 `configs/agent.yaml` 保持 `exporter.prometheus.enabled: false`。

