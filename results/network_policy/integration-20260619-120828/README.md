# NetworkPolicySkill YAML v2 集成测试记录

生成时间：`2026-06-19 12:08:28`

执行环境：

- 主机：`192.168.1.121`
- 系统：openEuler 24.03 LTS SP3
- 内核：`6.6.0-132.0.0.111.oe2403sp3.x86_64`

执行命令：

```bash
bash tests/integration/test_network_policy.sh
```

覆盖内容：

- `configs/skills.yaml` 使用 `schema_version: 2`。
- `network_policy` 通过 `targets.demo_cgroup` 和 `rules[].target_ref` 定位目标 cgroup。
- audit 模式不挂载 cgroup BPF。
- enforce 模式将动态端口 `18081` 写入 BPF `policy_map`。
- 目标 cgroup 内 curl 被拒绝，结果见 `enforce-curl.result`。
- 审计事件包含 `rule_id=deny_demo_port` 和 `target_ref=demo_cgroup`。
- Agent 退出后无 pinned link 或 cgroup attachment 残留。

结论边界：

- 本目录证明 `cgroup/connect4` 子能力已接入 YAML v2 目标/规则模型。
- 多规则并行和 k8s_pod target 仍属于后续工作。
