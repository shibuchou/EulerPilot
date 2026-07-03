# EulerPilot

EulerPilot 是一个面向 openEuler 的自适应资源管控 Agent。项目采用 eBPF 进行低开销观测，使用用户态 Agent 完成 workload 分类和策略决策，并同时支持：

- `cgroup v2` 主执行后端
- `sched_ext/scx` 正式对照后端

它不是单独的调度器样例，而是一套围绕比赛交付目标构建的闭环系统：

```text
eBPF Observer
-> Workload Analyzer
-> Policy Engine
-> CgroupExecutor / ScxExecutor
-> Benchmark / Report
```

---

## 当前状态

截至 `2026-07-03`，项目已经完成：

- `SP3 + cgroup v2` 主闭环
- `OLK-6.6 + sched_ext` 正式对照线
- `PsiGate v1` 远端闭环验证
- Redis `sched_ext` 正式候选结果：`RUNS=5`
- Nginx `sched_ext` 正式候选结果：`RUNS=5`
- 中文结果摘要、中文报告草稿和 SVG 图表材料
- Network Policy 阶段 B 最小闭环：
  - `network_policy`：cgroup/connect4 audit/enforce/rollback
  - `network_qos`：TC egress classifier + TBF 限速闭环
  - `network_qos` Benchmark：2 Mbit/s 目标下 121/122 实测误差约 -1.22% / -1.45%
  - `network_xdp`：isolated-veth generic XDP 已支持协议、源/目的 IP、源/目的端口匹配，覆盖 ICMP、TCP、UDP 和 UDP tuple 四规则闭环，rollback 事件输出 per-rule drop/byte 统计；真实 k3s lab Pod host veth generic XDP attach/drop/rollback 已在 121/122 通过
  - `TargetResolver`：`container/k8s_pod -> runtime PID -> netns -> host veth/ifindex` 已接入 Network QoS/XDP 真实 Pod host veth 验证
- Security Policy 阶段 C 最小闭环：
  - `security_policy`：YAML v2 `targets + rules + target_ref`
  - BPF LSM：`file_open`、`bprm_check_security`、`socket_connect`、`ptrace_traceme`、`capable`、`task_fix_setuid`、`task_fix_setgid`、`task_fix_setgroups` 与 `cred_prepare` enforce
  - file policy：`file_access=any/read/write`，已验证目标 cgroup 内读放行、精确路径写阻断和 `path_prefix` 只读目录写阻断
  - ptrace policy：`lsm/ptrace_traceme`，已验证仅目标 cgroup 内 `PTRACE_TRACEME` 被拒绝
  - capability policy：`lsm/capable`，已验证仅目标 cgroup 内 `CAP_SYS_ADMIN` 被拒绝
  - credential policy：`lsm/task_fix_setuid`、`lsm/task_fix_setgid`、`lsm/task_fix_setgroups` 与 `lsm/cred_prepare`，已验证仅目标 cgroup 内 setuid/setgid/setgroups credential 转换和 cred_prepare credential preparation 被拒绝，并输出 `uid/euid/suid/setuid_flags`、`gid/egid/sgid/setgid_flags`、`group_count/old_group_count`、`cred_gfp`
  - syscall tracing：`execve/openat/connect/ptrace` audit 观测
  - runtime anomaly：`anomaly_rules` 已支持 `burst_execve`、`burst_connect`、`burst_openat_sensitive`、`capability_abuse` 与 `credential_churn` 速率规则，基于 syscall/LSM ringbuf 事件在用户态聚合并输出 `operation=anomaly`；credential anomaly 会输出 `credential_stage` 以及对应 uid/gid/group/gfp 生命周期证据
  - target scope：path、path_prefix、file_access、exec_path、exec_prefix、socket endpoint、ptrace/capability/setuid/setgid/setgroups/cred_prepare cgroup scope、显式 cgroup、PID 自动解析、container_id cgroup tree 解析、container runtime name 解析、Kubernetes Pod 名称解析
  - 121/122 集成测试和 121 质量门禁通过
- Resource Control 阶段 D CPU+Memory+IO + target_ref 闭环：
  - `resource_control`：YAML v2 `controllers + targets + profiles` 配置已接入
  - CPU：`cpu.weight`、`cpu.max`、`cpuset.cpus`、`cpuset.mems`
  - Memory：`memory.high`、`memory.low`、`memory.max`
  - IO：`io.weight`、`io.max`，默认解析根文件系统块设备，pressure 模式限制 background 组写带宽
  - Target：`profiles.<name>.target_ref` 可绑定 cgroup/PID/container/container_id/k8s_pod，统一解析为 cgroup 后执行控制器写入
  - 执行动作：读取旧值、校验、写入、复读验证、`AuditBus` 事件、`ActionJournal` 记录、Agent 退出恢复旧值
  - 121/122 CPU+Memory+IO 集成测试通过，已验证 background pressure 下 `cpu.max=10000 100000`、`memory.high=1048576`、`io.max wbps=1048576`、`memory.events high` 与 `io.stat wbytes` 增长和 rollback 恢复
  - 121/122 `target_ref` 集成测试通过，已验证只对目标 cgroup 写 `cpu.max/memory.high`，非目标 cgroup 不被误改
  - 121/122 runtime target 集成测试通过，已验证 `container_id`、runtime container name 和 `k8s_pod` 名称解析后进入同一套 CPU/Memory 控制器写入、审计和 rollback
  - 121/122 真实 runtime readiness 已刷新为 Podman runtime ready、k3s Kubernetes lab ready；Docker 18.09 daemon 因 cgroup v2 devices controller 问题不作为主验证 runtime
  - 真实 runtime / Pod target 已转 pass：`test_resource_control_real_runtime_target.sh` 在 Podman + 本地 `localhost/eulerpilot-busybox:latest` 镜像下完成真实容器 cgroup 写入与 rollback；`test_resource_control_real_pod_target.sh` 在 `eulerpilot-lab/eulerpilot-rc-pod` 上完成 `type: k8s_pod` 的 Pod cgroup 写入、审计和 rollback
  - 121/122 CPU quota 效果测试通过，已用 `cpu.stat usage_usec/nr_throttled/throttled_usec` 证明 `cpu.max=10000 100000` 后单位时间 CPU 使用量约降至 10%，且 throttling 计数明显增加
  - 121/122 Redis + background CPU quota Compare/Sweep Benchmark 通过，已分离 `default_noisy`、`eulerpilot_no_quota` 与多档 `eulerpilot_quota` 阶段；同样 Agent 放置下，`quota_10` 在 121/122 的 background CPU ratio 分别为 `0.0247` / `0.0246`，并触发 throttling；sweep 结果显示 121 可尝试 `quota_05`，但 122 在 `0.85` RPS 保留阈值下无 profile 完全达标，因此跨机保守建议仍以 `quota_10` 作为 Redis 默认演示 profile，Redis GET/SET RPS 作为业务侧边界指标记录，不写成提升结论
  - 121/122 Nginx + background CPU quota Sweep Benchmark 通过，同样 Agent 放置下 `quota_05` 在两台机器均满足 `0.85` RPS 保留阈值，background CPU ratio 均为 `0.0125`；该结果作为 Nginx 场景的激进候选 profile 证据，不直接覆盖 Redis 的保守默认 profile
  - 121/122 Redis + Nginx 混合业务 quota Sweep Benchmark 通过，Redis GET/SET 与 Nginx wrk 同时运行；121 推荐 `quota_20`，122 推荐 `quota_50`，两端均证明 `quota_10` 可把 background CPU ratio 压到约 `0.025`，但混合业务下 Redis 保留率低于 `0.70`，因此混合演示 profile 需按业务保留率选择，不直接套用单 workload 最优值
  - 121/122 Redis + Nginx 多资源组合 profile Benchmark 通过，验证 `cpu.max + cpuset.cpus + memory.low/high` 组合写入、审计和 rollback；两端均推荐 `multi_quota50`，121/122 业务最低保留率分别为 `0.7302` / `0.7939`，background ratio 分别为 `0.1257` / `0.1257`
- Policy Engine 阶段 F 最小联动闭环：
  - 新增正式 `policy_engine` Skill，默认关闭，用于消费其他 Skill 的审计事件并下发可回滚处置动作
  - 已完成三条联动：`burst_execve -> resource_control cgroup 降级`、`burst_connect -> lab cgroup + lab netdev`、`burst_connect -> real Pod cgroup + real Pod host veth`
  - `policy_engine` 对 `type: k8s_pod` target 会按动作类型解析为 Pod cgroup 或 Pod host veth，事件保留 `target_type=k8s_pod`、`resolved_target_type=cgroup|netdev`、Pod namespace/name/UID 与统一 `transaction_id`
  - 写入流程复用保守事务语义：读取旧值、白名单动作校验、写入、复读验证、`reports/events/policy_engine.jsonl` 审计、`ActionJournal` 记录和 Agent 退出恢复旧值；121/122 已验证三条链路、rollback 和跨 Skill 证据链

当前最重要的候选结果目录为：

- Redis：`/root/EulerPilot/results/final/redis-scx-compare-20260612-191543`
- Nginx：`/root/EulerPilot/results/final/nginx-scx-compare-20260612-194018`
- Security scoped credential/cred_prepare 121：`/root/EulerPilot/results/security_policy/integration-20260624-114838`
- Security scoped credential/cred_prepare 122：`/root/EulerPilot/results/security_policy/integration-20260624-115440`
- Security 服务联动 anomaly 121：`/root/EulerPilot/results/security_policy/anomaly-rules-20260703-121-v4`
- Security 服务联动 anomaly 122：`/root/EulerPilot/results/security_policy/anomaly-rules-20260703-122-v2`
- Security credential anomaly 121：`/root/EulerPilot/results/security_policy/credential-anomaly-20260703-121-v3`
- Security credential anomaly 122：`/root/EulerPilot/results/security_policy/credential-anomaly-20260703-122-v3`
- Resource Control CPU+Memory 回归 121：`/root/EulerPilot/results/resource_control/integration-20260624-160317`
- Resource Control CPU+Memory 回归 122：`/root/EulerPilot/results/resource_control/integration-20260624-160349`
- Resource Control IO 121：`/root/EulerPilot/results/resource_control/io-20260624-160008`
- Resource Control IO 122：`/root/EulerPilot/results/resource_control/io-20260624-160208`
- Resource Control target_ref 121：`/root/EulerPilot/results/resource_control/target-20260624-172139`
- Resource Control target_ref 122：`/root/EulerPilot/results/resource_control/target-20260624-172916`
- Resource Control runtime target 121：`/root/EulerPilot/results/resource_control/runtime-target-20260630-113310`
- Resource Control runtime target 122：`/root/EulerPilot/results/resource_control/runtime-target-20260630-113354`
- Resource Control runtime readiness 121：`/root/EulerPilot/results/resource_control/runtime-readiness-20260630-k3s-121`
- Resource Control runtime readiness 122：`/root/EulerPilot/results/resource_control/runtime-readiness-20260630-k3s-122`
- Resource Control real runtime target 121：`/root/EulerPilot/results/resource_control/real-runtime-target-20260630-podman-121-final2`
- Resource Control real runtime target 122：`/root/EulerPilot/results/resource_control/real-runtime-target-20260630-podman-122-final2`
- Resource Control real Pod target 121：`/root/EulerPilot/results/resource_control/real-pod-target-20260630-k3s-121-v2`
- Resource Control real Pod target 122：`/root/EulerPilot/results/resource_control/real-pod-target-20260630-k3s-122-v1`
- Network QoS real Pod host veth 121：`/root/EulerPilot/results/network_policy/real-pod-veth-qos-20260630-k3s-121-v2`
- Network QoS real Pod host veth 122：`/root/EulerPilot/results/network_policy/real-pod-veth-qos-20260630-k3s-122-v1`
- Network XDP real Pod host veth ICMP/TCP/UDP 121：`/root/EulerPilot/results/network_policy/real-pod-veth-xdp-20260703-k3s-121-udp-v4`
- Network XDP real Pod host veth ICMP/TCP/UDP 122：`/root/EulerPilot/results/network_policy/real-pod-veth-xdp-20260703-k3s-122-udp-v4`
- Network XDP ICMP/TCP/UDP + UDP tuple isolated-veth 121：`/root/EulerPilot/results/network_policy/xdp-20260703-121-fields-v1`
- Network XDP ICMP/TCP/UDP + UDP tuple isolated-veth 122：`/root/EulerPilot/results/network_policy/xdp-20260703-122-fields-v1`
- Resource Control CPU quota 121：`/root/EulerPilot/results/resource_control/cpu-quota-20260625-095030`
- Resource Control CPU quota 122：`/root/EulerPilot/results/resource_control/cpu-quota-20260625-095114`
- Resource Control Redis quota Compare Benchmark 121：`/root/EulerPilot/results/resource_control/redis-quota-compare-20260625-102426`
- Resource Control Redis quota Compare Benchmark 122：`/root/EulerPilot/results/resource_control/redis-quota-compare-20260625-102611`
- Resource Control Redis quota Sweep Benchmark 121：`/root/EulerPilot/results/resource_control/redis-quota-sweep-20260626-203131`
- Resource Control Redis quota Sweep Benchmark 122：`/root/EulerPilot/results/resource_control/redis-quota-sweep-20260626-203505`
- Resource Control Nginx quota Sweep Benchmark 121：`/root/EulerPilot/results/resource_control/nginx-quota-sweep-20260626-210702`
- Resource Control Nginx quota Sweep Benchmark 122：`/root/EulerPilot/results/resource_control/nginx-quota-sweep-20260626-211057`
- Resource Control Mixed Redis+Nginx quota Sweep Benchmark 121：`/root/EulerPilot/results/resource_control/mixed-quota-sweep-20260627-102503`
- Resource Control Mixed Redis+Nginx quota Sweep Benchmark 122：`/root/EulerPilot/results/resource_control/mixed-quota-sweep-20260627-103139`
- Resource Control Mixed Redis+Nginx Multi-Resource Benchmark 121：`/root/EulerPilot/results/resource_control/mixed-multi-resource-20260628-211631`
- Resource Control Mixed Redis+Nginx Multi-Resource Benchmark 122：`/root/EulerPilot/results/resource_control/mixed-multi-resource-20260628-212132`
- Policy Engine Security -> Resource Control 联动 121：`/root/EulerPilot/results/policy_engine/security-resource-20260629-163949`
- Policy Engine Security -> Resource Control 联动 122：`/root/EulerPilot/results/policy_engine/security-resource-20260629-164135`
- Policy Engine Security -> Network + Resource lab 联动 121：`/root/EulerPilot/results/policy_engine/security-network-resource-20260629-214952`
- Policy Engine Security -> Network + Resource lab 联动 122：`/root/EulerPilot/results/policy_engine/security-network-resource-20260629-215950`
- Policy Engine real Pod Security -> Network + Resource 联动 121：`/root/EulerPilot/results/policy_engine/real-pod-security-network-resource-20260630-k3s-121-v1`
- Policy Engine real Pod Security -> Network + Resource 联动 122：`/root/EulerPilot/results/policy_engine/real-pod-security-network-resource-20260630-k3s-122-v1`
- 121 最新质量门禁：`/root/EulerPilot/reports/final_quality_gate_20260630-v32-real-pod-policy-121.log`

当前图表目录为：

- `/root/EulerPilot/reports/final_figures`

说明：

- 上述最终结果目录与图表目录已经回收到主交付仓库 `192.168.1.121:/root/EulerPilot`。
- `192.168.1.122` 当前作为第二台 openEuler 24.03 LTS SP3 验证环境，保留 `OLK-6.6 / sched_ext` 验证能力，并同步 Network/Security 关键结果。
- 本地当前主要保存代码镜像、脚本与中文文档入口。

---

## 快速入口

### 代码与实验

- Redis 主实验：`/root/EulerPilot/bench/redis/run_redis_main_experiment.sh`
- Redis sched_ext 正式 compare：`/root/EulerPilot/bench/redis/run_redis_sched_ext_compare.sh`
- Nginx 主实验：`/root/EulerPilot/bench/nginx/run_nginx_main_experiment.sh`
- Nginx sched_ext 正式 compare：`/root/EulerPilot/bench/nginx/run_nginx_sched_ext_compare.sh`
- Network connect4 集成测试：`/root/EulerPilot/tests/integration/test_network_policy.sh`
- Network TC QoS 集成测试：`/root/EulerPilot/tests/integration/test_network_qos_tc.sh`
- Network TC QoS 速率 Benchmark：`/root/EulerPilot/tests/benchmark/test_network_qos_rate.sh`
- Network XDP 集成测试：`/root/EulerPilot/tests/integration/test_network_xdp.sh`
- Network XDP 真实 Pod host veth 演示入口：`/root/EulerPilot/tests/integration/test_network_xdp_real_pod_veth.sh`
- Security Policy 集成测试：`/root/EulerPilot/tests/integration/test_security_policy.sh`
- Security anomaly 规则集成测试：`/root/EulerPilot/tests/integration/test_security_policy_anomaly_rules.sh`
- Security credential anomaly 集成测试：`/root/EulerPilot/tests/integration/test_security_policy_credential_anomaly.sh`
- Resource Control 集成测试：`/root/EulerPilot/tests/integration/test_resource_control.sh`
- Resource Control IO 集成测试：`/root/EulerPilot/tests/integration/test_resource_control_io.sh`
- Resource Control target_ref 集成测试：`/root/EulerPilot/tests/integration/test_resource_control_target.sh`
- Resource Control runtime target 集成测试：`/root/EulerPilot/tests/integration/test_resource_control_runtime_target.sh`
- Resource Control runtime readiness 诊断：`/root/EulerPilot/tests/integration/test_resource_control_runtime_readiness.sh`
- Resource Control 真实容器 target 演示入口：`/root/EulerPilot/tests/integration/test_resource_control_real_runtime_target.sh`
- Resource Control 真实 Pod target 演示入口：`/root/EulerPilot/tests/integration/test_resource_control_real_pod_target.sh`
- Resource Control CPU quota 效果测试：`/root/EulerPilot/tests/integration/test_resource_control_cpu_quota.sh`
- Resource Control Redis quota Compare Benchmark：`/root/EulerPilot/tests/benchmark/test_resource_control_redis_quota_compare.sh`
- Resource Control Redis quota Sweep Benchmark：`/root/EulerPilot/tests/benchmark/test_resource_control_redis_quota_sweep.sh`
- Resource Control Nginx quota Sweep Benchmark：`/root/EulerPilot/tests/benchmark/test_resource_control_nginx_quota_sweep.sh`
- Resource Control Mixed Redis+Nginx quota Sweep Benchmark：`/root/EulerPilot/tests/benchmark/test_resource_control_mixed_quota_sweep.sh`
- Resource Control Mixed Redis+Nginx Multi-Resource Benchmark：`/root/EulerPilot/tests/benchmark/test_resource_control_mixed_multi_resource.sh`
- Policy Engine Security -> Resource Control 联动测试：`/root/EulerPilot/tests/integration/test_policy_engine_security_resource.sh`
- Policy Engine Security -> Network + Resource lab 联动测试：`/root/EulerPilot/tests/integration/test_policy_engine_security_network_resource.sh`
- Policy Engine real Pod Security -> Network + Resource 联动测试：`/root/EulerPilot/tests/integration/test_policy_engine_real_pod_network_resource.sh`
- 质量门禁：`/root/EulerPilot/scripts/final_quality_gate.sh`

### 最终候选结果

- Redis：`/root/EulerPilot/results/final/redis-scx-compare-20260612-191543`
- Nginx：`/root/EulerPilot/results/final/nginx-scx-compare-20260612-194018`

### 图表材料

- `/root/EulerPilot/reports/final_figures/redis_sched_ext_rps.svg`
- `/root/EulerPilot/reports/final_figures/redis_sched_ext_p99.svg`
- `/root/EulerPilot/reports/final_figures/nginx_sched_ext_rps.svg`
- `/root/EulerPilot/reports/final_figures/nginx_sched_ext_p99.svg`
- `/root/EulerPilot/reports/final_figures/redis_quiet_overhead.svg`
- `/root/EulerPilot/reports/final_figures/nginx_quiet_overhead.svg`
- `/root/EulerPilot/reports/final_figures/psigate_timeline.svg`

---

## 当前推荐阅读顺序

如果要快速了解当前项目状态，推荐按下面顺序阅读：

1. `/root/EulerPilot/docs/contest_briefing_reference.md`
2. `/root/EulerPilot/docs/next_phase_plan_v2_1.md`
3. `/root/EulerPilot/docs/progress_status.md`
4. `/root/EulerPilot/docs/network_policy_skill.md`
5. `/root/EulerPilot/docs/security_policy_skill.md`
6. `/root/EulerPilot/docs/resource_control_skill.md`
7. `/root/EulerPilot/docs/skills_yaml_plan.md`
8. `/root/EulerPilot/docs/final_report_submission.md`
9. `/root/EulerPilot/docs/design_proposal.md`

如果要继续实现下一阶段功能，以 `docs/next_phase_plan_v2_1.md` 为当前执行口径；`docs/next_phase_plan_v2.md` 仅作为历史版本保留。

如果要快速定位代码主线，推荐按下面顺序阅读：

1. `agent/src/runtime.cpp`
2. `agent/src/executors.cpp`
3. `sched/scx_eulerpilot.bpf.c`
4. `bench/redis/`
5. `bench/nginx/`

---

## 当前结论边界

当前项目已经不再缺核心功能、正式实验和候选结果目录，但最终结论应保持谨慎：

- `sched_ext` 已经形成正式 compare 框架
- Redis / Nginx 都已经有多轮候选结果
- 某些模式在某些 workload 上表现出正向趋势
- 但不应简单概括为“全面优于默认调度器”

也就是说，当前项目已经进入：

> 正式能力增强、最终报告与答辩材料并行收口阶段。
