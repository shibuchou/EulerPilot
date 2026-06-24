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

截至 `2026-06-23`，项目已经完成：

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

当前最重要的候选结果目录为：

- Redis：`/root/EulerPilot/results/final/redis-scx-compare-20260612-191543`
- Nginx：`/root/EulerPilot/results/final/nginx-scx-compare-20260612-194018`
- Security scoped credential/cred_prepare 121：`/root/EulerPilot/results/security_policy/integration-20260624-114838`
- Security scoped credential/cred_prepare 122：`/root/EulerPilot/results/security_policy/integration-20260624-115440`

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
6. `/root/EulerPilot/docs/skills_yaml_plan.md`
7. `/root/EulerPilot/docs/final_report_submission.md`
8. `/root/EulerPilot/docs/design_proposal.md`

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
