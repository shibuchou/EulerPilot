# EulerPilot 答辩问答预案

更新时间：`2026-07-27`

## 1. 这是不是聊天型 AI Agent？

不是。EulerPilot 是系统自治型 Agent，核心是本地运行的观测、决策、执行和回滚闭环。它不依赖外部大模型 API，不把不可解释模型输出作为控制依据。

## 2. 为什么 SP3 主线使用 cgroup v2，而不是强行使用 sched_ext？

openEuler 24.03 LTS SP4 是当前核心验证和性能实验仓库。发行内核稳定控制路径采用 cgroup v2 的 CPU/Memory/IO 控制；sched_ext/scx 基于 SP4 官方源码自编译启用 `CONFIG_SCHED_CLASS_EXT` 的内核完成复核，不声称发行默认内核直接支持 sched_ext。SP3 121 是比赛要求的强制兼容交付环境，必须验证发行环境构建、cgroup v2 主闭环、安全扩展 smoke、rollback、safe doctor 和 sched_ext graceful fallback。

答辩口径：

```text
SP3 保证交付稳定性，SP4/scx 体现调度创新性。
```

## 3. 项目的创新点在哪里？

主要有四点：

1. `Observer -> Policy Engine -> Skill Manager -> Executor` 的 OS Agent 框架。
2. Resource、Network、Security、Policy Engine 都做成统一 Skill，并通过 YAML 启停。
3. Security anomaly 可以触发 Resource + Network 多动作联动，且支持事务化回滚。
4. 证据链完整：AuditBus、ActionJournal、结果目录、final evidence compact、Web Console 都能按同一条链路解释。

## 4. 多动作策略执行到一半失败怎么办？

Policy Engine 会把每个动作包装成白名单 action，执行前保存旧值。若 Resource 已写入但 Network QoS 失败，会按反向顺序恢复已经 applied 的动作，并写入 `policy_engine_events.jsonl`、对应 Skill 事件和 `ActionJournal`。

证据目录：

```text
results/policy_engine/security-network-resource-20260706-164539/failure-rollback/
```

## 5. Web Console 会不会影响实验结论？

不会。Web Console 是旁路展示控制台，所有状态、图表和结论均来自现有 CLI、测试脚本、事件日志和 evidence 文件。它本身不产生新的性能结论，不作为实验数据源。

## 6. Network Policy 如何避免误伤真实网卡？

v3.1 默认只允许 lab netdev，测试脚本创建 isolated veth，例如 `ep-veth-*`。默认拒绝真实业务网卡类名称，如 `eth*`、`ens*`、`eno*`、`wlan*`、`bond*`、`cni*`、`flannel*`。现场推荐只运行 `policy_engine_lab`，不要直接操作真实业务网卡。

## 7. Security Policy 只是 demo 吗？

不是。当前 BPF 文件和正式 Skill 口径已经去掉 demo 文件名，Security Policy 覆盖 LSM enforce、syscall tracing、服务联动 anomaly、credential anomaly 和 deep hook 评估。历史 `*_demo` Skill 名仅作为兼容别名保留，避免旧脚本失效。

## 8. Resource Control 覆盖了哪些资源？

已覆盖 CPU、Memory、IO 三类：

- CPU：`cpu.max`、`cpu.weight`
- Memory：`memory.high`、必要的 `memory.max` guard
- IO：`io.max`、`io.weight`

同时支持 cgroup、container runtime、真实 k3s Pod target 的解析和回滚验证。

## 9. 结果是否只在单机上成立？

不是。121/122 双机完成了 SP3 主路径与历史对照验证；SP4 主机现在作为核心验证仓库，已完成自编译 sched_ext/scx 增强路径、Web Console 和 Kubernetes 旁路验证。Redis/Nginx RUNS=10 历史结果已保留为 provisional evidence，最终收益数字以 formal artifact RUNS=10 结果为准。当前 evidence manifest 将这些结果和降级状态统一收口到 42 条核心证据。

## 10. 如果现场环境波动怎么办？

优先使用 Web Console 的“离线证据演示”和 `docs/final_evidence_index.md`。现场 live 只跑白名单、短时、可 cleanup 的链路：

```text
环境检查 -> 查看 Skills -> 状态 JSON -> doctor -> policy_engine_lab -> cleanup
```

## 11. 当前最明确的限制是什么？

1. SP3 默认内核不保证 sched_ext 可用。
2. SP4/scx 路径依赖自编译或已启用 `CONFIG_SCHED_CLASS_EXT` 的内核。
3. Web Console 是展示和白名单演示层，不替代 C++ Agent 控制面。
4. Kubernetes 高级演示依赖现场集群状态，默认不作为首选现场按钮。

## 12. 评委如果只看三份证据，应看什么？

1. `docs/final_submission_guide.md`
2. `reports/final_evidence_compact.md`
3. `results/policy_engine/security-network-resource-20260706-164539/report.md`
