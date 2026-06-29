# EulerPilot 最终演示 Runbook

更新时间：`2026-06-29`

本文用于现场答辩和本地复现。演示脚本入口为 `demo/demo_all_final.sh`，支持 live、offline 和 cleanup 三种模式。

## 模式

```bash
sudo demo/demo_all_final.sh --mode live
demo/demo_all_final.sh --mode offline
sudo demo/demo_all_final.sh --cleanup
```

- `live`：真实运行环境检查、配置校验、doctor、v3.1 Policy Engine 第二条联动测试和 rollback 验证。
- `offline`：不改系统状态，只展示仓库内已保存的结果目录、图表、summary 和 report 位置。
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

## offline 演示材料

优先展示以下文件：

- `docs/final_evidence_index.md`
- `docs/final_results_summary.md`
- `docs/policy_engine_skill.md`
- `docs/network_policy_skill.md`
- `docs/security_policy_skill.md`
- `docs/resource_control_skill.md`
- `results/policy_engine/security-network-resource-*/report.md`
- `results/policy_engine/security-network-resource-*/summary.txt`

## 现场注意事项

- live 模式需要 root 权限，因为会创建 cgroup、veth/netns 并写入 qdisc。
- `network_qos` 只允许操作 `ep-*`、`eulerpilot-*`、`lab-*` 前缀的 lab netdev；演示脚本创建 `ep-veth-pe0 <-> ep-veth-pe1`，不操作生产网卡。
- 如果现场环境缺少 `iperf3`，测试脚本会使用 Python TCP rate probe 兜底。
- SP4 和 Kubernetes/真实 runtime 不作为 v3.1 live 演示完成条件；相关计划见 `docs/sp4_validation_plan.md`，v3.2 第一优先级继续补 K8s 真实 Pod target。