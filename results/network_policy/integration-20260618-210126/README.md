# NetworkPolicySkill 集成测试记录

生成时间：`2026-06-18 21:01:26`

执行环境：

- 主机：`192.168.1.121`
- 系统：openEuler 24.03 LTS SP3
- 内核：`6.6.0-132.0.0.111.oe2403sp3.x86_64`

执行命令：

```bash
bash tests/integration/test_network_policy.sh
```

覆盖内容：

- `network_policy` 正式 Skill 注册名可被 `--list-skills` 枚举。
- audit 模式下 `--doctor-skills` 和 Agent 运行通过，且不挂载 cgroup BPF。
- enforce 模式下 YAML 动态端口 `18081` 写入 BPF `policy_map`。
- 目标 cgroup 内 curl 连接被拒绝，结果见 `enforce-curl.result`。
- Agent 退出后无 pinned link 或 cgroup attachment 残留。

结论边界：

- 本目录只证明 `cgroup/connect4` 子能力的 audit/enforce、动态端口、命中统计和 rollback。
- TC QoS 与 isolated-veth XDP 仍属于后续阶段。
