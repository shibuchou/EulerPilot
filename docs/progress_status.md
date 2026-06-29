# EulerPilot 进度状态看板

更新时间：`2026-06-28`

当前执行口径：`docs/next_phase_plan_v2_1.md`

## 当前阶段

阶段 B：Network Policy 完整实现，状态：`收尾 / container + Pod veth 真实解析预备已完成`

阶段 C：Security Agent 正式化，状态：`BPF LSM + socket_connect + bprm exec_prefix + file_access/path_prefix + ptrace_traceme + capable + task_fix_setuid + task_fix_setgid + task_fix_setgroups + cred_prepare + syscall tracing + runtime anomaly + 多目标 target_map + 规则级事件标识 + cgroup/pid/container/runtime/pod scope 最小闭环已完成`

阶段 D：Resource Control，状态：`CPU + Memory + IO 自动闭环、target_ref/runtime target 闭环、Redis/Nginx 单项与混合 quota sweep、多资源组合 profile 证据已完成；真实 runtime / Pod target 演示入口已完成，现场 pass 等待 runtime/kubectl 环境`

目标：

- 将 `network_policy_demo` 升级为正式 `network_policy` Skill。
- 先完成 `cgroup/connect4` 的 `audit/enforce/status/rollback` 正式口径。
- 再补 TC QoS 和 isolated-veth XDP，并继续扩展 Benchmark、多规则和 Pod veth。
- `TargetResolver` 已从 netdev 与 `k8s_pod` 诊断入口推进到 container name/ID、Pod UID、runtime PID、netns 和 host veth/ifindex 真实解析；后续重点是把真实集群 lab Pod 接入 TC/XDP 演示。
- 所有 Network 事件接入 `AuditBus`，所有挂载/卸载动作接入 `ActionJournal`。
- Resource Control 已从 CPU-only 扩展到 CPU + Memory + IO：pressure 模式下写 `cpu.max`、`memory.high`、`io.weight` 与 `io.max`，latency 组使用 `memory.low` 保护，IO 默认解析根文件系统块设备，并通过事务化写入、`AuditBus`、`ActionJournal` 和 stop rollback 闭环验证；`target_ref` 已接入 `TargetResolver`，可将 profile 绑定到 cgroup/PID/container/Pod 解析出的真实 cgroup；121/122 已完成 `container_id`、runtime container name 和 `k8s_pod` 名称解析的 runtime target 集成测试，并用 `cpu.stat usage_usec/nr_throttled/throttled_usec` 证明 CPU quota 实际生效；Redis/Nginx quota Sweep Benchmark 已拆分 `default_noisy`、`eulerpilot_no_quota` 和多档 `eulerpilot_quota` 阶段，当前结论限定为同样 Agent 放置下后台限额效果显著，业务 RPS 只作为边界证据；Redis 跨机保守默认演示 profile 为 `quota_10`，Nginx 跨机激进候选为 `quota_05`；Redis+Nginx 混合业务 sweep 显示 121 推荐 `quota_20`、122 推荐 `quota_50`，混合场景必须同时看 Redis GET/SET 与 Nginx RPS 保留率，不能直接套用单 workload 最优 profile；真实 runtime readiness 诊断已在 121/122 完成，当前两台机器都缺少 docker/podman/containerd/crictl/kubectl 与 Kubernetes lab；真实容器与 Pod target 演示脚本已补齐，当前分别输出 `missing-docker-or-podman` 和 `missing-kubectl` 的 blocked 证据，待 runtime/kubectl/lab Pod 补齐后可直接转为现场 pass。

## 阶段完成情况

| 阶段 | 状态 | 当前结论 | 主要证据 |
|------|------|----------|----------|
| A. 公共基础设施 | 已完成 | 远端 Git、文档规则、README 覆盖、公共控制面最小代码和现有质量门禁已完成；后续随正式 Skill 深度接入 | `AGENTS.md`、本文件、各目录 README、`docs/public_control_plane_design.md`、`reports/final_quality_gate_20260618_control_plane.log` |
| B. Network Policy | 收尾 / container + Pod veth 真实解析预备已完成 | 正式 `network_policy` 注册名已落地；connect4 audit/enforce 已完成；TC QoS 最小闭环与速率误差 Benchmark 已完成；isolated-veth XDP 多规则闭环已完成；schema v2 `targets + rules + target_ref` 已落地；`TargetResolver` 已支持 netdev、container name/ID、Pod UID、runtime container ID/PID、netns path 和 host veth/ifindex 解析；`network_qos` 与 `network_xdp` 已可接受 `type: container` / `type: k8s_pod` target 并解析成 host veth ifname；下一步在真实 Kubernetes lab Pod 上跑 TC/XDP 演示 | `docs/network_policy_skill.md`、`docs/network_pod_veth_target.md`、`tests/integration/test_network_policy.sh`、`tests/integration/test_network_qos_tc.sh`、`tests/integration/test_network_xdp.sh`、`tests/integration/test_target_resolver.sh`、`tests/benchmark/test_network_qos_rate.sh` |
| C. Security Agent | 九类 LSM + 四类 syscall tracing + runtime anomaly + 多目标 target_map + 规则级事件标识 + cgroup/pid/container/runtime/pod scope 最小闭环已完成 | 正式 `security_policy` 注册名已落地；YAML v2 target/rule、最多 8 项 BPF `target_map`、audit BPF attach 不阻断、enforce BPF LSM blocked hit、`lsm_socket_connect`、`lsm_bprm_check_security` exec_prefix、`lsm_file_open` file_access/path_prefix、`lsm_ptrace_traceme`、`lsm_capable`、`lsm_task_fix_setuid`、`lsm_task_fix_setgid`、`lsm_task_fix_setgroups`、`lsm_cred_prepare`、`burst_execve` 用户态异常规则、规则级事件、显式 cgroup/PID/container_id/runtime container/k8s_pod scope 和 rollback 无残留已在 121/122 通过；下一步转向 cred_transfer/cred_alloc_blank 等更多 cred 生命周期规则、更多异常行为规则和联动处置 | `docs/security_policy_skill.md`、`agent/skills/security_policy/README.md`、`tests/integration/test_security_policy.sh`、`demo/security_policy_demo/README.md` |
| D. Resource Control | CPU + Memory + IO 自动闭环、target_ref/runtime target 闭环、Redis/Nginx 单项与混合 quota sweep、多资源组合 profile 证据已完成；真实 runtime / Pod target 演示入口已完成，现场 pass 等待 runtime/kubectl 环境 | 正式 `resource_control` 已读取 YAML v2 `controllers + targets + profiles`；cgroup v2 后端已支持 `cpu.weight/cpu.max/cpuset`、`memory.high/memory.low/memory.max` 与 `io.weight/io.max`；写入流程包含旧值读取、值校验、写入、复读验证、`AuditBus` 事件、`ActionJournal` 记录和 Agent stop rollback；121/122 集成测试已验证 background pressure `cpu.max=10000 100000`、`memory.high=1048576`、`io.max wbps=1048576`、`io.weight=default 50`、`memory.events high` 增长、`io.stat wbytes` 增长、写入吞吐下降和旧值恢复；121/122 target 测试已验证显式 cgroup、`container_id`、runtime container name 和 `k8s_pod` 名称解析均只作用于目标 cgroup，非目标 cgroup 不被误改；CPU quota 测试已验证限额后 `usage_rate_ratio=0.1007` 且 `nr_throttled/throttled_usec` 明显增加；Redis quota Compare Benchmark 已验证同样 Agent 放置下后台 cgroup CPU 使用率降至 no-quota 阶段约 2.47% / 2.50%；Redis quota sweep 给出跨机保守默认 profile `quota_10`，Nginx quota sweep 在 121/122 均推荐 `quota_05` 作为激进候选；Mixed Redis+Nginx sweep 已验证同一窗口并发压测时 background ratio 仍可按 profile 下降，121 推荐 `quota_20`、122 推荐 `quota_50`，说明混合业务需以多前台最低保留率选 profile；Mixed multi-resource benchmark 已验证 `cpu.max + cpuset.cpus + memory.low/high` 组合 profile，两端均推荐 `multi_quota50`；真实 runtime / Pod target 演示脚本已完成，当前因 121/122 缺 docker/podman/kubectl 记录 blocked 证据，待环境补齐后可直接转为 pass | `docs/resource_control_skill.md`、`tests/integration/test_resource_control.sh`、`tests/integration/test_resource_control_io.sh`、`tests/integration/test_resource_control_target.sh`、`tests/integration/test_resource_control_runtime_target.sh`、`tests/integration/test_resource_control_runtime_readiness.sh`、`tests/integration/test_resource_control_real_runtime_target.sh`、`tests/integration/test_resource_control_real_pod_target.sh`、`tests/integration/test_resource_control_cpu_quota.sh`、`tests/benchmark/test_resource_control_redis_quota.sh`、`tests/benchmark/test_resource_control_redis_quota_compare.sh`、`tests/benchmark/test_resource_control_redis_quota_sweep.sh`、`tests/benchmark/test_resource_control_nginx_quota_sweep.sh`、`tests/benchmark/test_resource_control_mixed_quota_sweep.sh`、`tests/benchmark/test_resource_control_mixed_multi_resource.sh`、`results/resource_control/integration-20260624-160317`、`results/resource_control/integration-20260624-160349`、`results/resource_control/io-20260624-160008`、`results/resource_control/io-20260624-160208`、`results/resource_control/target-20260624-172139`、`results/resource_control/target-20260624-172916`、`results/resource_control/runtime-target-20260624-212403`、`results/resource_control/runtime-target-20260624-212529`、`results/resource_control/runtime-readiness-20260628-214925`、`results/resource_control/runtime-readiness-20260628-215010`、`results/resource_control/real-runtime-target-20260628-215812`、`results/resource_control/real-runtime-target-20260628-215854`、`results/resource_control/real-pod-target-20260628-220051`、`results/resource_control/real-pod-target-20260628-220106`、`results/resource_control/cpu-quota-20260625-095030`、`results/resource_control/cpu-quota-20260625-095114`、`results/resource_control/redis-quota-compare-20260625-102426`、`results/resource_control/redis-quota-compare-20260625-102611`、`results/resource_control/redis-quota-sweep-20260626-203131`、`results/resource_control/redis-quota-sweep-20260626-203505`、`results/resource_control/nginx-quota-sweep-20260626-210702`、`results/resource_control/nginx-quota-sweep-20260626-211057`、`results/resource_control/mixed-quota-sweep-20260627-102503`、`results/resource_control/mixed-quota-sweep-20260627-103139`、`results/resource_control/mixed-multi-resource-20260628-211631`、`results/resource_control/mixed-multi-resource-20260628-212132` |
| E. SP4/sched_ext 复核 | 未开始 | 等待 SP4/123 环境 | `docs/next_phase_plan_v2_1.md` |
| F. Kubernetes 与跨 Agent 联动 | 未开始 | 等待 Network/Security/Resource 正式 Skill | `docs/next_phase_plan_v2_1.md` |
| G. Benchmark 与冻结材料 | 未开始 | 等待正式能力完成 | `docs/next_phase_plan_v2_1.md` |

## 当前已完成基线

- 赛题宣讲资料已独立为 `docs/contest_briefing_reference.md`。
- 下一阶段争奖计划已升级为 `docs/next_phase_plan_v2_1.md`。
- `docs/skills_yaml_plan.md` 已升级为 v2.1 YAML/Skill 控制面规划。
- 121/122 已有 `.gitignore` 与 `.gitattributes`。
- 本地镜像已从 121 重新同步并建立 Git 初始提交。
- 121 已初始化真实 Git 仓库，当前基线提交为 `d81f920 Initialize EulerPilot remote baseline`。
- 项目自有关键顶层目录 README 已补齐：`agent/`、`bench/`、`bpf/`、`configs/`、`dashboard/`、`demo/`、`docs/`、`reports/`、`results/`、`sched/`、`scripts/`、`tools/`。
- 121 现有质量门禁已复测通过，日志为 `reports/final_quality_gate_20260618_stage_a.log`。
- 公共控制面最小代码已落地：
  - `agent/include/capability_detector.hpp` / `agent/src/capability_detector.cpp`
  - `agent/include/target_resolver.hpp` / `agent/src/target_resolver.cpp`
  - `agent/include/audit_bus.hpp` / `agent/src/audit_bus.cpp`
  - `agent/include/action_journal.hpp` / `agent/src/action_journal.cpp`
- `--doctor-skills` 已输出 Capability Detector 摘要，覆盖 BTF、BPF LSM、XDP、TC、cgroup v2、PSI、sched_ext、memory.reclaim、Kubernetes 等能力。
- 公共控制面改动后的 Agent smoke 已通过，日志为 `reports/agent_smoke_20260618_control_plane.log`。
- 公共控制面改动后的完整质量门禁已通过，日志为 `reports/final_quality_gate_20260618_control_plane.log`。
- 121 提交记录：
  - `d81f920 Initialize EulerPilot remote baseline`
  - `6219b82 Document stage A baseline progress`
  - `d7fbee6 Add stage A control plane foundation`
- 122 已同步公共控制面代码，并完成 `make -B agent` 与 `--doctor-skills` 检查；122 doctor 显示 `sched_ext` available。

## 阶段 A 任务收口

| 编号 | 任务 | 状态 | 说明 |
|------|------|------|------|
| A1 | 121 初始化 Git 仓库 | 已完成 | 远端基线提交：`d81f920 Initialize EulerPilot remote baseline` |
| A2 | 关键目录 README 补齐 | 已完成 | 项目自有关键顶层目录均已有 README |
| A3 | 文档规则写入 `AGENTS.md` | 已完成 | 已要求阶段收口同步更新文档、README 和进度看板 |
| A4 | 公共控制面接口与最小实现 | 已完成 | 设计文档：`docs/public_control_plane_design.md`；`CapabilityDetector` 已接入 `--doctor-skills`，其余三项已提供可编译最小接口 |
| A5 | 质量门禁基线复测 | 已完成 | `reports/final_quality_gate_20260618_stage_a.log`，12 项 P0 与 optional checks 均通过 |

## 阶段 B 立即任务

| 编号 | 任务 | 状态 | 说明 |
|------|------|------|------|
| B1 | NetworkPolicySkill 文档升级 | 已完成 | `docs/network_policy_skill.md` 已固定正式 Skill 目标、YAML、事件和回滚口径 |
| B2 | `network_policy_demo` 到 `network_policy` 迁移方案 | 已完成 | 正式注册名 `network_policy` 已增加，`network_policy_demo` 保留兼容 |
| B3 | connect4 audit/enforce | 已完成 | audit 模式不挂 BPF；enforce 模式使用 BPF map 动态配置端口，`stats_map` 记录 allow/deny，rollback 后无 attachment 残留 |
| B4 | TC QoS | 已完成最小闭环 | `network_qos` 使用 TC egress BPF classifier 统计命中，TBF qdisc 执行限速；已验证 lab netns/veth、audit/enforce 和 rollback |
| B5 | YAML v2 targets/rules | 已完成最小闭环 | `configs/skills.yaml` 已升级为 `schema_version: 2`；connect4 与 TC QoS 均通过 `target_ref` 解析目标 |
| B6 | isolated-veth XDP | 已完成多规则闭环 | `network_xdp` 使用 generic XDP 在专用 lab veth 上执行 ICMP drop 与 TCP:19092 drop；已验证 audit/enforce、多规则 drop 统计和 rollback 后连通性恢复 |
| B7 | TC QoS 速率误差 Benchmark | 已完成 | `tests/benchmark/test_network_qos_rate.sh` 使用 Python TCP rate probe 验证 2 Mbit/s TBF 限速；121 误差 -1.22%，122 误差 -1.45% |
| B8 | Pod veth target 解析预备 | 已完成真实解析预备 | `TargetResolver` 已支持 netdev ifname 校验、ifindex 解析、`kubectl` Pod UID/container ID 查询、runtime PID 查询、`/proc/<pid>/ns/net` 记录和 host veth/ifindex 反查；`tests/integration/test_target_resolver.sh` 使用临时 netns/veth + fake `kubectl/crictl` 验证成功路径，不依赖真实 Kubernetes |
| B9 | Container veth target 解析预备 | 已完成真实解析预备 | `resolve_container_netdev_target` 已支持 container ID 或 runtime container name 解析 PID、netns path 和 host veth/ifindex；`network_qos/network_xdp` v2 target 已接受 `type: container` |

## 阶段 B 当前证据

- 正式 `network_policy` Skill 已注册，`network_policy_demo` 保留兼容。
- `configs/skills.yaml` 已升级为 `schema_version: 2`，`network_policy`、`network_qos` 与 `network_xdp` 均使用 `targets + rules + target_ref`。
- `network_policy` 默认 disabled，默认模式为 `audit`。
- `tests/integration/test_network_policy.sh` 已验证：
  - `network_policy` 能被 `--list-skills` 枚举。
  - audit 模式下 `--doctor-skills` 通过。
  - audit 模式下 Agent 能运行。
  - audit 模式不挂载 cgroup BPF，不会阻断流量。
  - audit 模式会写入 `reports/events/network_policy.jsonl`。
- enforce 模式已验证：
  - YAML 中 `dst_port: '18081'` 会写入 BPF `policy_map`。
  - 目标 cgroup 内 curl 被 cgroup/connect4 拒绝，结果为 `rc=7 http_code=000`。
  - rollback 事件包含 `deny_count=1`，证明 `stats_map` 生效。
  - Agent 退出后 `/sys/fs/bpf/eulerpilot_network_policy_link` 和 cgroup BPF attachment 无残留。
- 121 最新集成测试证据目录：`results/network_policy/integration-20260619-142347/`。
- 122 最新集成测试证据目录：`results/network_policy/integration-20260619-122352/`。
- 121 最新完整质量门禁已通过：`reports/final_quality_gate_20260628_resource_real_runtime_target.log`。

### YAML v2 证据

- `SkillManager` 已支持 `schema_version: 1/2`，并将嵌套 YAML flatten 为现有 `SkillSpec.config`，避免大改 Skill 接口。
- `network_policy` 已优先读取 `rules.*.hook=cgroup_connect4` 和 `targets.<target_ref>.type=cgroup`。
- `network_qos` 已优先读取 `rules.*.hook=tc_egress`，并支持 `targets.<target_ref>.type=netdev/container/k8s_pod`。
- `network_xdp` 已优先读取 `rules.*.hook=xdp`，并支持 `targets.<target_ref>.type=netdev/container/k8s_pod`。
- 审计事件已带上 v2 规则和目标：
  - connect4：`rule_id=deny_demo_port`，`target_ref=demo_cgroup`。
  - TC QoS：`rule_id=limit_lab_egress`，`target_ref=lab_veth`。
  - XDP：`rule_id=drop_icmp_lab`，`target_ref=lab_xdp_veth`。
  - 121 验证事件位于 `reports/events/network_policy.jsonl`。

### TC QoS 证据

- 新增正式子能力 `network_qos`，默认 disabled。
- 新增 BPF 程序 `bpf/network_qos_tc.bpf.c`，提供 `tc_egress` classifier 命中统计。
- 新增构建目标 `make network-qos-tc`。
- 新增清理脚本 `scripts/cleanup_network_qos_tc.sh`，负责删除 lab veth/netns、root qdisc 和 clsact。
- 新增集成测试 `tests/integration/test_network_qos_tc.sh`，已验证：
  - `network_qos` 能被 `--list-skills` 枚举。
  - 专用 lab netns/veth 基线连通。
  - audit 模式不修改 TC qdisc。
  - enforce 模式安装 TC clsact + TBF。
  - ping 流量命中 BPF stats，rollback 事件记录 `packet_count=3`、`byte_count=294`。
  - Agent 退出后无 TC qdisc 残留。
- 121 最新 TC QoS 集成测试证据目录：`results/network_policy/qos-tc-20260619-142357/`。
- 122 最新 TC QoS 集成测试证据目录：`results/network_policy/qos-tc-20260619-122403/`。

### TC QoS Benchmark 证据

- 新增工具 `tools/tcp_rate_probe.py`，只依赖 Python 标准库，负责在 lab veth 上输出 baseline/enforce TCP 吞吐。
- 新增 Benchmark `tests/benchmark/test_network_qos_rate.sh`，验证 `network_qos` 在 2 Mbit/s TBF 目标下的实际限速误差。
- Benchmark 会关闭 lab veth 的 TSO/GSO/GRO 并使用小块 TCP 发送，避免 veth/GSO 聚合导致 TBF 误差失真。
- 121 最新 QoS rate Benchmark 目录：`results/network_policy/qos-rate-20260620-181708/`。
  - baseline：`1607.461 Mbit/s`
  - enforce：`1.976 Mbit/s`
  - 目标：`2.000 Mbit/s`
  - 误差：`-1.22%`
  - 降幅：`813.65x`
- 122 最新 QoS rate Benchmark 目录：`results/network_policy/qos-rate-20260620-181755/`。
  - baseline：`1796.228 Mbit/s`
  - enforce：`1.971 Mbit/s`
  - 目标：`2.000 Mbit/s`
  - 误差：`-1.45%`
  - 降幅：`911.33x`

### XDP 证据

- 正式子能力 `network_xdp` 默认 disabled。
- `bpf/network_xdp_demo.bpf.c` 已从单规则扩展为最多 8 条 XDP 规则，提供 generic XDP filter 和 `pass/drop/byte` 聚合统计。
- 新增构建目标 `make network-xdp-demo`。
- 新增清理脚本 `scripts/cleanup_network_xdp_demo.sh`，负责删除 lab veth/netns 并尝试卸载 XDP。
- 新增集成测试 `tests/integration/test_network_xdp.sh`，已验证：
  - `network_xdp` 能被 `--list-skills` 枚举。
  - 专用 lab netns/veth 基线连通。
  - audit 模式不挂 XDP。
  - enforce 模式在 `ep-veth-xdp0` 上挂 generic XDP 并 drop ICMP。
  - enforce 模式同时验证 TCP:19092 规则命中。
  - rollback 事件记录多规则 `drop_count >= 2`。
  - Agent 退出后无 XDP attachment 残留，连通性恢复。
- 121 最新 XDP 多规则集成测试证据目录：`results/network_policy/xdp-20260620-183031/`。
- 122 最新 XDP 多规则集成测试证据目录：`results/network_policy/xdp-20260620-184212/`。
- 121 最新完整质量门禁已通过：`reports/final_quality_gate_20260628_resource_real_runtime_target.log`，21/21 P0、100 轮 Agent smoke 和 5 轮 doctor 均通过。

### TargetResolver / container + Pod veth 预备证据

- `resolve_netdev_target` 已增加 ifname 安全校验和 ifindex 解析，非法 ifname 返回 `invalid-ifname`，不存在的设备返回 `netdev-not-found`。
- `resolve_container_netdev_target` 已提供 runtime container name/ID 到 PID、netns path 和 host veth/ifindex 的真实解析入口，默认不要求 Kubernetes 存在。
- `resolve_k8s_pod_target` 已提供默认安全的真实解析入口，默认只接受 `eulerpilot-lab` namespace；会通过 `kubectl` 查询 Pod UID/container ID，通过 runtime CLI 查询 PID，读取 netns path，并用 `nsenter + ip -o link` 解析 host veth/ifindex。
- `network_qos` 与 `network_xdp` 的 v2 target 解析已接受 `type: container`、`type: k8s_pod` / `type: pod`，成功解析后复用 host veth ifname 执行后续 TC/XDP 逻辑。
- `docs/network_pod_veth_target.md` 已记录 reason code、安全边界和接入要求。
- `tests/integration/test_target_resolver.sh` 已在 121 通过，覆盖 netdev 成功/失败路径、`k8s_pod` 的 `unsupported-namespace`、`missing-kubectl`、`missing-runtime` 诊断路径，以及临时 netns/veth + fake `kubectl/crictl` 的 container 和 Pod host veth 解析成功路径。
- 121 container veth 解析预备证据目录：`results/network_policy/target-resolver-container-20260623-112000/`。
- 121/122 Pod veth 解析预备证据目录已同步：
  - `results/network_policy/target-resolver-20260623-103436/`
  - `results/network_policy/target-resolver-20260623-103729/`

## 阶段 C 当前证据

- 正式 `security_policy` 已注册，`security_policy_demo` 保留为兼容回归入口。
- `configs/skills.yaml` 已新增 `security_policy`，默认 `enabled: false`、`mode: audit`，使用 schema v2 `targets + rules + target_ref` 描述 demo path target。
- `docs/security_policy_skill.md` 已补 SecurityPolicySkill 正式化说明，明确 audit/enforce、BPF LSM 安全边界、target 过滤、事件输出、回滚清理和参考代码复用边界。
- `agent/skills/security_policy/README.md` 已说明当前正式入口最小能力和后续未完成项：容器 target 绑定、规则级事件标识和进程过滤。
- `demo/security_policy_demo/README.md` 已补最小 BPF LSM demo 的运行手册和风险边界，覆盖 YAML 下发的 demo secret 文件、demo deny_exec 脚本和最多 8 项 target_map。
- `tests/integration/test_security_policy.sh` 已在 121/122 真实验证 `make agent security-policy-demo`、BPF LSM attach、四类 syscall tracepoint 事件、目标文件拒绝、demo 执行拒绝、scoped IPv4 socket connect 拒绝、scoped writable-dir exec_prefix 执行拒绝、scoped `file_access=write` 精确路径写打开拒绝且读打开放行、scoped `path_prefix + file_access=write` 只读目录写打开拒绝且读打开放行、scoped `lsm_ptrace_traceme` 拒绝、scoped `lsm_capable` / `CAP_SYS_ADMIN` 拒绝、scoped `lsm_task_fix_setuid` / setuid credential 转换拒绝、scoped `lsm_task_fix_setgid` / setgid credential 转换拒绝、scoped `lsm_task_fix_setgroups` / supplementary groups 转换拒绝、双动态 `/tmp` YAML target_map、Agent 退出恢复和 cleanup 无残留；同时验证 `burst_execve` 异常规则输出 `operation=anomaly/result=observed`。
- 121 最新 Security demo 集成测试证据目录：`results/security_policy/integration-20260621-095537/`。
- 122 最新 Security demo 集成测试证据目录：`results/security_policy/integration-20260621-100937/`。
- 121 最新正式 `security_policy` audit/enforce 集成测试证据目录：`results/security_policy/integration-20260621-101929/`。
- 122 最新正式 `security_policy` audit/enforce 集成测试证据目录：`results/security_policy/integration-20260621-103431/`。
- 121 最新正式 `security_policy` BPF ringbuf hit 集成测试证据目录：`results/security_policy/integration-20260621-104254/`。
- 122 最新正式 `security_policy` BPF ringbuf hit 集成测试证据目录：`results/security_policy/integration-20260621-105602/`。
- 121 最新正式 `security_policy` syscall tracing 集成测试证据目录：`results/security_policy/integration-20260621-110631/`。
- 122 最新正式 `security_policy` syscall tracing 集成测试证据目录：`results/security_policy/integration-20260621-113455/`。
- 121 最新正式 `security_policy` 四类 syscall tracing 集成测试证据目录：`results/security_policy/integration-20260621-150713/`。
- 122 最新正式 `security_policy` 四类 syscall tracing 集成测试证据目录：`results/security_policy/integration-20260621-151229/`。
- 121 最新正式 `security_policy` 双 LSM enforce 集成测试证据目录：`results/security_policy/integration-20260621-152838/`。
- 122 最新正式 `security_policy` 双 LSM enforce 集成测试证据目录：`results/security_policy/integration-20260621-153514/`。
- 121 最新正式 `security_policy` target_map 动态路径集成测试证据目录：`results/security_policy/integration-20260621-161943/`。
- 122 最新正式 `security_policy` target_map 动态路径集成测试证据目录：`results/security_policy/integration-20260621-162111/`。
- 121 最新正式 `security_policy` 多目标 target_map 集成测试证据目录：`results/security_policy/integration-20260621-164838/`。
- 122 最新正式 `security_policy` 多目标 target_map 集成测试证据目录：`results/security_policy/integration-20260621-165001/`。
- 121 最新正式 `security_policy` 规则级事件标识集成测试证据目录：`results/security_policy/integration-20260621-171904/`。
- 122 最新正式 `security_policy` 规则级事件标识集成测试证据目录：`results/security_policy/integration-20260621-171957/`。
- 121 最新正式 `security_policy` cgroup scope 集成测试证据目录：`results/security_policy/integration-20260621-173942/`。
- 122 最新正式 `security_policy` cgroup scope 集成测试证据目录：`results/security_policy/integration-20260621-174042/`。
- 121 最新正式 `security_policy` PID target 集成测试证据目录：`results/security_policy/integration-20260621-175927/`。
- 122 最新正式 `security_policy` PID target 集成测试证据目录：`results/security_policy/integration-20260621-180029/`。
- 121 最新正式 `security_policy` container_id target 集成测试证据目录：`results/security_policy/integration-20260621-211502/`。
- 122 最新正式 `security_policy` container_id target 集成测试证据目录：`results/security_policy/integration-20260621-211701/`。
- 121 最新正式 `security_policy` runtime/Pod target 集成测试证据目录：`results/security_policy/integration-20260621-214903/`。
- 122 最新正式 `security_policy` runtime/Pod target 集成测试证据目录：`results/security_policy/integration-20260621-215158/`。
- 121 最新正式 `security_policy` socket_connect LSM 集成测试证据目录：`results/security_policy/integration-20260622-105820/`。
- 122 最新正式 `security_policy` socket_connect LSM 集成测试证据目录：`results/security_policy/integration-20260622-110120/`。
- 121 最新正式 `security_policy` bprm exec_prefix LSM 集成测试证据目录：`results/security_policy/integration-20260622-145403/`。
- 122 最新正式 `security_policy` bprm exec_prefix LSM 集成测试证据目录：`results/security_policy/integration-20260622-145716/`。
- 121 最新正式 `security_policy` file_access LSM 集成测试证据目录：`results/security_policy/integration-20260622-195342/`。
- 122 最新正式 `security_policy` file_access LSM 集成测试证据目录：`results/security_policy/integration-20260622-195627/`。
- 121 最新正式 `security_policy` ptrace LSM 集成测试证据目录：`results/security_policy/integration-20260623-094234/`。
- 122 最新正式 `security_policy` ptrace LSM 集成测试证据目录：`results/security_policy/integration-20260623-094924/`。
- 121 最新正式 `security_policy` capable LSM 集成测试证据目录：`results/security_policy/integration-20260623-143856/`。
- 122 最新正式 `security_policy` capable LSM 集成测试证据目录：`results/security_policy/integration-20260623-145107/`。
- 121 最新正式 `security_policy` read-only directory 集成测试证据目录：`results/security_policy/integration-20260623-152931/`。
- 122 最新正式 `security_policy` read-only directory 集成测试证据目录：`results/security_policy/integration-20260623-153318/`。
- 121 最新正式 `security_policy` scoped credential/cred_prepare LSM 集成测试证据目录：`results/security_policy/integration-20260624-114838/`。
- 122 最新正式 `security_policy` scoped credential/cred_prepare LSM 集成测试证据目录：`results/security_policy/integration-20260624-115440/`。
- audit 模式证据：目标文件和 demo 执行脚本保持可访问，`security_policy_events.audit.jsonl` 包含 `operation=hit`、`result=observed`、`enforce=0`，并覆盖 `event_hook=lsm_file_open/lsm_bprm_check_security/sys_enter_execve/sys_enter_openat/sys_enter_connect/sys_enter_ptrace`；异常证据见 `security_policy_events.anomaly-execve.jsonl`，包含 `operation=anomaly`、`rule_id=burst_execve`、`event_hook=sys_enter_execve`、`threshold/window_ms/hit_count`。
- enforce 模式证据：目标文件和 demo 执行脚本被 BPF LSM 拒绝，`security_policy_events.enforce.jsonl` 包含 `operation=hit`、`result=blocked`、`enforce=1`；动态多目标 blocked 事件会携带单条 `rule_id/target_ref` 和 `target_index`；cgroup scoped、PID scoped、container_id scoped、runtime container scoped 与 k8s pod scoped 事件会携带 `cgroup_id/cgroup_path`，且 scope 外访问保持成功；`security_policy_events.socket.jsonl` 包含 `event_hook=lsm_socket_connect`、`dst_ip=127.0.0.1`、`dst_port`、`protocol=tcp` 和 `result=blocked`；`security_policy_events.exec-prefix.jsonl` 包含 `event_hook=lsm_bprm_check_security`、形如 `/tmp/eulerpilot-security-policy.<suffix>/` 的 `exec_prefix`、`cgroup_id` 和 `result=blocked`；`security_policy_events.file-access.jsonl` 包含 `event_hook=lsm_file_open`、`file_access=write`、`file_flags`、`cgroup_id` 和 `result=blocked`，同时测试脚本验证同一目标在 scoped cgroup 内读打开成功、写打开失败；`security_policy_events.readonly-dir.jsonl` 包含 `event_hook=lsm_file_open`、`path_prefix`、`file_access=write`、`file_flags`、`cgroup_id`、`rule_id=deny_readonly_dir_write`、`target_ref=readonly_dir` 和 `result=blocked`，同时测试脚本验证目录前缀下文件在 scoped cgroup 内读打开成功、写打开失败；`security_policy_events.ptrace.jsonl` 包含 `event_hook=lsm_ptrace_traceme`、`path=ptrace_traceme`、`cgroup_id`、`rule_id=deny_ptrace_traceme`、`target_ref=ptrace_scope` 和 `result=blocked`；`security_policy_events.capable.jsonl` 包含 `event_hook=lsm_capable`、`capability=CAP_SYS_ADMIN`、`cgroup_id`、`rule_id=deny_cap_sys_admin`、`target_ref=capable_scope` 和 `result=blocked`；`security_policy_events.setuid.jsonl` 包含 `event_hook=lsm_task_fix_setuid`、`uid/euid/suid/setuid_flags`、`cgroup_id`、`rule_id=deny_setuid_transition`、`target_ref=setuid_scope` 和 `result=blocked`；`security_policy_events.setgid.jsonl` 包含 `event_hook=lsm_task_fix_setgid`、`gid/egid/sgid/setgid_flags`、`cgroup_id`、`rule_id=deny_setgid_transition`、`target_ref=setgid_scope` 和 `result=blocked`；`security_policy_events.setgroups.jsonl` 包含 `event_hook=lsm_task_fix_setgroups`、`group_count/old_group_count`、`cgroup_id`、`rule_id=deny_setgroups_transition`、`target_ref=setgroups_scope` 和 `result=blocked`；`security_policy_events.cred-prepare.jsonl` 包含 `event_hook=lsm_cred_prepare`、`uid/euid/suid/gid/egid/sgid/group_count/old_group_count/cred_gfp`、`cgroup_id`、`rule_id=deny_cred_prepare_transition`、`target_ref=cred_prepare_scope` 和 `result=blocked`；Agent 退出后恢复可访问，无 BPF link/pin 残留。
- `scripts/cleanup_security_policy_demo.sh` 已修复无残留时因 `grep`/`pipefail` 返回非零的问题，cleanup 空跑现在正常返回 0。

## 阶段 D 当前任务收口

| 编号 | 任务 | 状态 | 说明 |
|------|------|------|------|
| D1 | ResourceControlPolicy 配置模型 | 121/122 已完成 | `resource_control.config` 已支持 `mode`、CPU/Mem/IO controller 开关、IO 设备 `auto` 解析、`targets` 和 latency/batch/background profile |
| D2 | CPU+Memory+IO 事务化执行 | 121/122 已完成 | `agent/src/executors.cpp` 已支持 `cpu.max`、`memory.high/low/max`、`io.weight`、`io.max`，每次写入记录旧值、复读验证、写审计和 journal，stop 时恢复旧值 |
| D3 | cgroup v2 初始化与回滚脚本 | 121/122 已完成 | `scripts/setup_cgroup_v2.sh` 尝试开启 `cpu/cpuset/memory/io`；`scripts/rollback.sh` 恢复 CPU/Mem 默认值，并将 `io.weight` 恢复为 `default 100`、`io.max` 恢复为不限速 |
| D4 | CPU+Memory 集成测试 | 121/122 已完成 | `tests/integration/test_resource_control.sh` 结果目录：121 `results/resource_control/integration-20260624-160317`；122 `results/resource_control/integration-20260624-160349` |
| D5 | IO controller 集成测试 | 121/122 已完成 | `tests/integration/test_resource_control_io.sh` 结果目录：121 `results/resource_control/io-20260624-160008`；122 `results/resource_control/io-20260624-160208`；target_ref 改动后 121 回归 `results/resource_control/io-regression-20260624-174400`；验证 `io.max/io.weight` 写入、`io.stat wbytes` 增长、限速耗时上升和 rollback |
| D6 | container/Pod target 接入 | 121/122 已完成 runtime target 解析闭环 | `target_ref` 已接入 `TargetResolver`，支持 `type: cgroup/pid/container_id/container/k8s_pod` 解析到 cgroup path；`tests/integration/test_resource_control_target.sh` 已在 121/122 验证显式 cgroup target 只限制目标 cgroup；`tests/integration/test_resource_control_runtime_target.sh` 已在 121/122 验证 `container_id`、runtime container name 和 `k8s_pod` 名称解析后只限制目标 cgroup，非目标 cgroup 不被误改 |
| D7 | CPU quota 效果指标 | 121/122 已完成 | `tests/integration/test_resource_control_cpu_quota.sh` 已在 121/122 验证 `cpu.max=10000 100000` 后 `usage_usec` 单位时间增量降至基线约 10%，且 `nr_throttled/throttled_usec` 增长；结果目录：121 `results/resource_control/cpu-quota-20260625-095030`，122 `results/resource_control/cpu-quota-20260625-095114` |
| D8 | Redis + background CPU quota Compare Benchmark | 121/122 已完成 | `tests/benchmark/test_resource_control_redis_quota_compare.sh` 已在 121/122 拆分 `default_noisy`、`eulerpilot_no_quota`、`eulerpilot_quota` 三阶段；121 quota 阶段 background CPU 使用率为 no-quota 的 `0.0247`，122 为 `0.0250`，两端 `quota_nr_throttled_delta=17`，Redis RPS 作为业务侧边界指标记录，未写成性能提升结论 |
| D9 | 真实 runtime readiness 诊断 | 121/122 已完成，真实实测环境阻塞 | `tests/integration/test_resource_control_runtime_readiness.sh` 已在 121/122 只读检查 docker/podman/nerdctl/ctr/crictl/kubectl、systemd 服务、runtime socket、runtime cgroup 和 Kubernetes lab；两端均为 `result=blocked`、`reason=missing-container-runtime-and-kubernetes-lab`，下一步需要安装或启动真实 runtime，或提供 `eulerpilot-lab` demo Pod 后再做现场实测 |
| D10 | Redis quota profile sweep | 121/122 已完成 | `tests/benchmark/test_resource_control_redis_quota_sweep.sh` 已在 121/122 扫描 `max/50%/20%/10%/5%` background `cpu.max`；121 在 `0.85` RPS 保留阈值下推荐 `quota_05`，background ratio `0.0121`；122 无 profile 同时满足 `0.85`，最佳折中为 `quota_10`，background ratio `0.0246`；因此跨机保守默认演示 profile 暂定 `cpu.max=10000 100000` |
| D11 | Nginx quota profile sweep | 121/122 已完成 | `tests/benchmark/test_resource_control_nginx_quota_sweep.sh` 已在 121/122 使用 `nginx + wrk + background CPU hog` 扫描 `max/50%/20%/10%/5%` background `cpu.max`；两端均推荐 `quota_05`，background ratio 均为 `0.0125`，RPS ratio 分别为 `1.0012` 和 `1.1841`；该 profile 作为 Nginx 场景激进候选，不覆盖 Redis 默认保守 profile |
| D12 | Mixed Redis+Nginx quota profile sweep | 121/122 已完成 | `tests/benchmark/test_resource_control_mixed_quota_sweep.sh` 已在 121/122 同时运行 Redis GET/SET 与 Nginx wrk，扫描相同 background `cpu.max` profile；121 推荐 `quota_20`，business min ratio `0.7073`、background ratio `0.0501`；122 推荐 `quota_50`，business min ratio `0.7681`、background ratio `0.1255`；两端 `quota_10` background ratio 均约 `0.025`，但混合业务最低保留率低于 `0.70`，因此混合场景需按多前台业务最低保留率选 profile |
| D13 | Mixed Redis+Nginx multi-resource profile | 121/122 已完成 | `tests/benchmark/test_resource_control_mixed_multi_resource.sh` 已在 121/122 比较 CPU/cpuset 与 `cpu.max + cpuset.cpus + memory.low/high` 组合 profile；两端均推荐 `multi_quota50`，121 business min ratio `0.7302`、background ratio `0.1257`，122 business min ratio `0.7939`、background ratio `0.1257`；JSONL 审计已验证 `cpuset.cpus`、latency `memory.low=67108864`、background `memory.high=134217728` 的 applied/restored |
| D14 | 真实 runtime / Pod target 演示入口 | 121/122 已完成脚本，当前环境 blocked | 新增 `tests/integration/test_resource_control_real_runtime_target.sh` 和 `tests/integration/test_resource_control_real_pod_target.sh`；真实 runtime 脚本在 docker/podman + 本地镜像可用时验证 `type: container` 的 `target_ref`、`cpu.max/memory.high` 写入、审计和 rollback，当前 121/122 均为 `missing-docker-or-podman`；真实 Pod 脚本在 `kubectl + eulerpilot-lab` demo Pod 可用时验证 `type: k8s_pod`，当前 121/122 均为 `missing-kubectl` |

## 阶段 A 后续随阶段接入项

这些内容不阻塞阶段 B 开始，但应在 Network/Security/Resource 正式接入时继续完善：

- `TargetResolver` 后续从当前 fake runtime 自测扩展到真实 Kubernetes lab Pod 实测，并把解析结果纳入 `network_qos/network_xdp` 演示脚本。
- `AuditBus` 从 Security 固定 path ringbuf 命中事件扩展到动态规则和更多 LSM hook 命中事件，Network/Resource 继续补全统一字段。
- `ActionJournal` 从 Security 生命周期动作扩展到动态 map 规则、container target 和异常恢复。
- `CapabilityDetector` 输出并入未来 `--status --json`。

## 进度维护规则

- 每完成阶段或关键子任务，必须更新本文件。
- 每新增目录，必须同时新增该目录的 `README.md`。
- 每个实验结果目录需要包含生成命令、环境、关键指标和结论边界。
- 每个正式 Skill 需要有独立设计文档、README、演示脚本和集成测试入口。
