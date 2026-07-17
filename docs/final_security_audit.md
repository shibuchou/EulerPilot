# EulerPilot 最终安全与质量审计报告

更新时间：`2026-07-08`

## 审计范围

对 EulerPilot 在 openEuler 24.03 LTS SP3 历史验证线和 SP4 主验证线上的安全默认值、运行时残留、内存泄漏替代验证、稳定性进行最终提交前审查。7 月 3 日 121 TAP 结果作为安全专项历史证据保留；当前最终质量门禁以 SP4 `results/k8s/sp4-validation-20260708-023552/final_quality_gate.log` 为准。

## 1. 安全默认值审计

| 检查项 | 期望值 | 实际值 | 结果 |
|--------|--------|--------|------|
| `network_policy_demo` 默认状态 | `enabled: false` | `enabled: false` | PASS |
| `security_policy` 默认状态 | `enabled: false` | `enabled: false` | PASS |
| `security_policy_demo` 默认状态 | `enabled: false` | `enabled: false` | PASS |
| metrics exporter 默认状态 | `enabled: false` | `enabled: false` | PASS |
| metrics exporter 监听地址 | `127.0.0.1:9108` | `127.0.0.1:9108` | PASS |
| 无硬编码密码/token/API key | 0 处 | 0 处 | PASS |
| skills.yaml 不被脚本自动修改 | 不自动修改 | 不自动修改 | PASS |
| agent.yaml 不被脚本自动修改 | 不自动修改 | 不自动修改 | PASS |

## 2. BPF/cgroup 残留审计

| 检查项 | 目标 | 实际 | 结果 |
|--------|------|------|------|
| security LSM link 残留 | 不存在 | 不存在 | PASS |
| network demo cgroup 残留 | 无 attached BPF | 不存在 | PASS |
| sched_ext state | `disabled` | `disabled` | PASS |
| sched_ext nr_rejected | `0` | `0` | PASS |

正式 `security_policy` 默认 disabled，audit 模式 attach BPF LSM + `execve/openat/connect/ptrace` tracepoint，但通过 `policy_map.enforce=0` 保持允许，命中后写 ringbuf observed hit；enforce 模式复用 `security_policy_demo` 的 BPF LSM，由用户态从 YAML target 写入最多 8 项 `target_map`，拒绝文件路径/前缀、执行路径/前缀、scoped IPv4 endpoint、scope-only ptrace、capability、setuid、setgid、setgroups 和 cred_prepare target。scope-only LSM target 会写入 `hook_type`，避免同一 cgroup 内多个 credential 规则误映射。LSM blocked/hit 携带 BPF `target_index` 并映射回单条 YAML `rule_id/target_ref`；scoped target 事件会写 `cgroup_id/cgroup_path`；socket、exec_prefix、file_access、ptrace、capability 和 credential 事件分别输出对应证据。`tests/integration/test_security_policy.sh` 已在 121/122 回归九类 LSM enforce、四类 syscall tracing、双动态 `/tmp` target_map、scoped writable-dir exec_prefix、file_access、path_prefix、ptrace_traceme、CAP_SYS_ADMIN、setuid/setgid/setgroups credential 转换、cred_prepare credential preparation、显式 cgroup/PID/container/runtime/Pod target 和 rollback 恢复，最新结果目录两端均为 `results/security_policy/integration-20260703-151721`。`test_security_policy_credential_anomaly.sh` 已在 121/122 回归 `credential_churn` 生命周期 anomaly，结果目录为 `results/security_policy/credential-anomaly-20260703-121-v4` 和 `results/security_policy/credential-anomaly-20260703-122-v4`。`test_security_policy_credential_deep_hooks.sh` 已在 121/122 验证 `lsm_cred_alloc_blank/lsm_cred_transfer` scoped 配置和 Agent attach，结果目录为 `results/security_policy/credential-deep-hooks-20260703-121-v2` 和 `results/security_policy/credential-deep-hooks-20260703-122-v2`；普通用户态 workload 未稳定触发这两个 hook，报告只记录 hit 计数，不声称稳定业务命中。security_policy_demo 在 rollback 时不 pin link，退出后无残留。

## 3. 内存泄漏审计

| 检查项 | 工具 | 结果 |
|--------|------|------|
| Valgrind 3.25.1 | leak-check=full | 无法启动：glibc 2.38 / ld-linux 兼容性问题 |
| 替代验证 | 100 轮 Agent 启动/退出 smoke | 全部正常退出，无异常 |

**判定**：Valgrind 在 openEuler 24.03 / glibc 2.38 上存在已知兼容性问题，不作为阻塞门禁。100 轮行为测试未发现内存泄漏导致的异常退出、卡死或资源残留。

## 4. 死锁/卡死审计

| 检查项 | 方法 | 结果 |
|--------|------|------|
| Agent 15s smoke | timeout 20s 单轮 | PASS |
| Agent 100 轮连续 smoke | for i in 1..100 | PASS |
| doctor 5 轮连续 probe | for i in 1..5 | PASS |
| SkillManager stop_all 清理 | 顺序执行 | 无 hang |
| metrics exporter 退出 | kill -INT | 无 hang |

## 5. 构建与回归审计

| 环境 | 检查项 | 结果 |
|------|--------|------|
| 121 | make agent | PASS |
| 121 | make network-policy | PASS |
| 121 | make network-qos-tc | PASS |
| 121 | make network-xdp | PASS |
| 121 | make security-policy | PASS |
| 121 | tests/integration/test_target_resolver.sh | PASS |
| 121 | tests/integration/test_security_policy.sh（正式 `security_policy` audit/enforce） | PASS |
| 122 | make agent + make security-policy | PASS |
| 122 | tests/integration/test_target_resolver.sh | PASS |
| 122 | tests/integration/test_security_policy.sh（正式 `security_policy` audit/enforce） | PASS |
| 121 | --list-skills 输出正式 network_policy/network_qos/network_xdp/security_policy | PASS |
| 121 | --doctor-skills 返回 0 | PASS |
| 122 | make agent | PASS |
| 122 | --list-skills 输出正式 Network 子能力 | PASS |
| 122 | --doctor-skills 返回 0 | PASS |

## 6. TAP 质量门禁结果

当前最终门禁：

- 日志：`results/k8s/sp4-validation-20260708-023552/final_quality_gate.log`
- 结果：22/22 P0、100 轮 Agent smoke、5 轮 doctor 通过

历史安全专项门禁：

最新日志：`reports/final_quality_gate_20260703-creddeep-121.log`

```
ok 1 - make agent
ok 2 - make network-policy
ok 3 - make network-qos-tc
ok 4 - make network-xdp
ok 5 - make security-policy
ok 6 - --list-skills outputs formal network and security policy skills
ok 7 - --doctor-skills exit 0
ok 8 - agent 15s smoke
ok 9 - network_policy default disabled
ok 10 - network_qos default disabled
ok 11 - network_xdp default disabled
ok 12 - security_policy default disabled
ok 13 - security_policy_demo default disabled
ok 14 - metrics default disabled on 127.0.0.1
ok 15 - dashboard index.html exists and non-empty
ok 16 - frozen result dirs exist (Redis=7, Nginx=3)
ok 17 - resource_control CPU+Memory+IO evidence
ok 18 - resource_control target_ref evidence
ok 19 - resource_control runtime target evidence
ok 20 - resource_control CPU quota effect evidence
ok 21 - no BPF/LSM/TC/XDP residue

# optional checks
ok - agent 100-round stress smoke
ok - doctor 5-round stable
```

## 7. 最终判定

| 审计项 | 结果 |
|--------|------|
| 安全默认值 | PASS |
| BPF/cgroup 残留 | PASS |
| 内存泄漏 | WARN (Valgrind 工具限制，100 轮 smoke 替代) |
| 死锁/卡死 | PASS |
| 构建/回归 | PASS |
| 质量门禁 | PASS；SP4 最终门禁 22/22，历史安全专项门禁 21/21 |

**当前结论：EulerPilot 通过最新安全与质量审计，可作为当前争奖增强阶段的稳定基线。Security 已具备正式 `security_policy` 最小 audit/enforce 闭环、file_open/bprm/socket_connect/ptrace_traceme/capable/task_fix_setuid/task_fix_setgid/task_fix_setgroups/cred_prepare 九类 LSM enforce、cred_alloc_blank/cred_transfer scoped attach 评估、四类 syscall tracing、最多 8 项 target_map、规则级 LSM blocked/hit 事件标识、显式 cgroup scope、PID target、container_id target、runtime container name target、k8s pod name target、scoped IPv4 endpoint、scoped writable-dir exec_prefix、scoped file_access、scoped path_prefix、scoped ptrace_traceme、scoped CAP_SYS_ADMIN、scoped setuid/setgid/setgroups credential 转换、scoped cred_prepare credential preparation 阻断、`credential_churn` 生命周期 anomaly、anomaly 进程过滤与组合 scope 过滤；最终证据压缩已覆盖核心证据，下一步转向答辩材料冻结和现场演示压测。**
