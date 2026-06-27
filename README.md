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

截至 `2026-06-26`，项目已经完成：

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
  - `network_xdp`：isolated-veth generic XDP ICMP + TCP 多规则闭环
  - `TargetResolver`：`container/k8s_pod -> runtime PID -> netns -> host veth/ifindex` 解析预备能力
- Security Policy 阶段 C 最小闭环：
  - `security_policy`：YAML v2 `targets + rules + target_ref`
  - BPF LSM：`file_open`、`bprm_check_security`、`socket_connect`、`ptrace_traceme`、`capable`、`task_fix_setuid`、`task_fix_setgid`、`task_fix_setgroups` 与 `cred_prepare` enforce
  - file policy：`file_access=any/read/write`，已验证目标 cgroup 内读放行、精确路径写阻断和 `path_prefix` 只读目录写阻断
  - ptrace policy：`lsm/ptrace_traceme`，已验证仅目标 cgroup 内 `PTRACE_TRACEME` 被拒绝
  - capability policy：`lsm/capable`，已验证仅目标 cgroup 内 `CAP_SYS_ADMIN` 被拒绝
  - credential policy：`lsm/task_fix_setuid`、`lsm/task_fix_setgid`、`lsm/task_fix_setgroups` 与 `lsm/cred_prepare`，已验证仅目标 cgroup 内 setuid/setgid/setgroups credential 转换和 cred_prepare credential preparation 被拒绝，并输出 `uid/euid/suid/setuid_flags`、`gid/egid/sgid/setgid_flags`、`group_count/old_group_count`、`cred_gfp`
  - syscall tracing：`execve/openat/connect/ptrace` audit 观测
  - runtime anomaly：`anomaly_rules` 已支持 `burst_execve` 速率规则，基于 `sys_enter_execve` ringbuf 事件在用户态聚合并输出 `operation=anomaly`
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
  - 121/122 真实 runtime readiness 诊断已完成，当前两台机器均缺少 docker/podman/containerd/crictl/kubectl 和 Kubernetes lab，因此真实容器 / Pod target 现场实测被环境阻塞；已有诊断结果明确下一步需要安装或启动真实 runtime，或提供 `eulerpilot-lab` namespace 与 demo Pod
  - 121/122 CPU quota 效果测试通过，已用 `cpu.stat usage_usec/nr_throttled/throttled_usec` 证明 `cpu.max=10000 100000` 后单位时间 CPU 使用量约降至 10%，且 throttling 计数明显增加
  - 121/122 Redis + background CPU quota Compare/Sweep Benchmark 通过，已分离 `default_noisy`、`eulerpilot_no_quota` 与多档 `eulerpilot_quota` 阶段；同样 Agent 放置下，`quota_10` 在 121/122 的 background CPU ratio 分别为 `0.0247` / `0.0246`，并触发 throttling；sweep 结果显示 121 可尝试 `quota_05`，但 122 在 `0.85` RPS 保留阈值下无 profile 完全达标，因此跨机保守建议仍以 `quota_10` 作为 Redis 默认演示 profile，Redis GET/SET RPS 作为业务侧边界指标记录，不写成提升结论
  - 121/122 Nginx + background CPU quota Sweep Benchmark 通过，同样 Agent 放置下 `quota_05` 在两台机器均满足 `0.85` RPS 保留阈值，background CPU ratio 均为 `0.0125`；该结果作为 Nginx 场景的激进候选 profile 证据，不直接覆盖 Redis 的保守默认 profile

当前最重要的候选结果目录为：

- Redis：`/root/EulerPilot/results/final/redis-scx-compare-20260612-191543`
- Nginx：`/root/EulerPilot/results/final/nginx-scx-compare-20260612-194018`
- Security scoped credential/cred_prepare 121：`/root/EulerPilot/results/security_policy/integration-20260624-114838`
- Security scoped credential/cred_prepare 122：`/root/EulerPilot/results/security_policy/integration-20260624-115440`
- Resource Control CPU+Memory 回归 121：`/root/EulerPilot/results/resource_control/integration-20260624-160317`
- Resource Control CPU+Memory 回归 122：`/root/EulerPilot/results/resource_control/integration-20260624-160349`
- Resource Control IO 121：`/root/EulerPilot/results/resource_control/io-20260624-160008`
- Resource Control IO 122：`/root/EulerPilot/results/resource_control/io-20260624-160208`
- Resource Control target_ref 121：`/root/EulerPilot/results/resource_control/target-20260624-172139`
- Resource Control target_ref 122：`/root/EulerPilot/results/resource_control/target-20260624-172916`
- Resource Control runtime target 121：`/root/EulerPilot/results/resource_control/runtime-target-20260624-212403`
- Resource Control runtime target 122：`/root/EulerPilot/results/resource_control/runtime-target-20260624-212529`
- Resource Control runtime readiness 121：`/root/EulerPilot/results/resource_control/runtime-readiness-20260625-104844`
- Resource Control runtime readiness 122：`/root/EulerPilot/results/resource_control/runtime-readiness-20260625-104857`
- Resource Control CPU quota 121：`/root/EulerPilot/results/resource_control/cpu-quota-20260625-095030`
- Resource Control CPU quota 122：`/root/EulerPilot/results/resource_control/cpu-quota-20260625-095114`
- Resource Control Redis quota Compare Benchmark 121：`/root/EulerPilot/results/resource_control/redis-quota-compare-20260625-102426`
- Resource Control Redis quota Compare Benchmark 122：`/root/EulerPilot/results/resource_control/redis-quota-compare-20260625-102611`
- Resource Control Redis quota Sweep Benchmark 121：`/root/EulerPilot/results/resource_control/redis-quota-sweep-20260626-203131`
- Resource Control Redis quota Sweep Benchmark 122：`/root/EulerPilot/results/resource_control/redis-quota-sweep-20260626-203505`
- Resource Control Nginx quota Sweep Benchmark 121：`/root/EulerPilot/results/resource_control/nginx-quota-sweep-20260626-210702`
- Resource Control Nginx quota Sweep Benchmark 122：`/root/EulerPilot/results/resource_control/nginx-quota-sweep-20260626-211057`
- 121 最新质量门禁：`/root/EulerPilot/reports/final_quality_gate_20260626_resource_nginx_quota_sweep.log`

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
- Security Policy 集成测试：`/root/EulerPilot/tests/integration/test_security_policy.sh`
- Resource Control 集成测试：`/root/EulerPilot/tests/integration/test_resource_control.sh`
- Resource Control IO 集成测试：`/root/EulerPilot/tests/integration/test_resource_control_io.sh`
- Resource Control target_ref 集成测试：`/root/EulerPilot/tests/integration/test_resource_control_target.sh`
- Resource Control runtime target 集成测试：`/root/EulerPilot/tests/integration/test_resource_control_runtime_target.sh`
- Resource Control runtime readiness 诊断：`/root/EulerPilot/tests/integration/test_resource_control_runtime_readiness.sh`
- Resource Control CPU quota 效果测试：`/root/EulerPilot/tests/integration/test_resource_control_cpu_quota.sh`
- Resource Control Redis quota Compare Benchmark：`/root/EulerPilot/tests/benchmark/test_resource_control_redis_quota_compare.sh`
- Resource Control Redis quota Sweep Benchmark：`/root/EulerPilot/tests/benchmark/test_resource_control_redis_quota_sweep.sh`
- Resource Control Nginx quota Sweep Benchmark：`/root/EulerPilot/tests/benchmark/test_resource_control_nginx_quota_sweep.sh`
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
