# EulerPilot 最终安全与质量审计报告

更新时间：`2026-06-21`

## 审计范围

对 EulerPilot v0.1-rc2 在 openEuler 24.03 LTS SP3 环境上的安全默认值、运行时残留、内存泄漏、稳定性进行最终提交前审查。

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

正式 `security_policy` 默认 disabled，audit 模式 attach BPF LSM + `execve/openat/connect/ptrace` tracepoint，但通过 `policy_map.enforce=0` 保持允许，命中后写 ringbuf observed hit；enforce 模式当前复用 `security_policy_demo` 的 BPF LSM，由用户态从 YAML `path/exec_path/cgroup_path` 写入最多 8 项 `target_map`，写 blocked hit 后拒绝访问 map 中的文件路径和执行路径。LSM blocked hit 会携带 BPF `target_index`，用户态据此还原单条 YAML `rule_id/target_ref`；配置 `cgroup_path` 时，BPF 还要求当前进程 cgroup id 命中，事件会写 `cgroup_id/cgroup_path`；tracepoint 观测事件不参与规则匹配，写 `target_index=unknown`。BPF link 默认不 pin，LSM 与 tracepoint 程序随 Agent 进程退出自动消亡。`tests/integration/test_security_policy.sh` 已在 121/122 验证 `lsm_file_open/lsm_bprm_check_security/sys_enter_execve/sys_enter_openat/sys_enter_connect/sys_enter_ptrace` 六类 BPF ringbuf hit、双 LSM enforce、双动态 `/tmp` target_map、规则级 blocked 事件、显式 cgroup scope 和 rollback 恢复，最新结果目录分别为 `results/security_policy/integration-20260621-173942` 和 `results/security_policy/integration-20260621-174042`；121/122 之前已验证规则级 blocked 事件，结果目录分别为 `results/security_policy/integration-20260621-171904` 和 `results/security_policy/integration-20260621-171957`；121/122 之前已验证多目标 target_map，结果目录分别为 `results/security_policy/integration-20260621-164838` 和 `results/security_policy/integration-20260621-165001`；121/122 之前已验证单动态 target_map，结果目录分别为 `results/security_policy/integration-20260621-161943` 和 `results/security_policy/integration-20260621-162111`；121/122 之前已验证双 LSM enforce，结果目录分别为 `results/security_policy/integration-20260621-152838` 和 `results/security_policy/integration-20260621-153514`；121/122 之前已验证四类 syscall tracing，结果目录分别为 `results/security_policy/integration-20260621-150713` 和 `results/security_policy/integration-20260621-151229`；正式 `security_policy` audit/enforce 已在 121/122 通过，结果目录分别为 `results/security_policy/integration-20260621-101929` 和 `results/security_policy/integration-20260621-103431`；兼容 demo 路径已在 121/122 验证 attach、deny、Agent 退出恢复和 cleanup 无残留，结果目录分别为 `results/security_policy/integration-20260621-095537` 和 `results/security_policy/integration-20260621-100937`。network_policy_demo 在 rollback 时 unpin + destroy，退出后无残留。

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
| 121 | make network-policy-demo | PASS |
| 121 | make network-qos-tc | PASS |
| 121 | make network-xdp-demo | PASS |
| 121 | make security-policy-demo | PASS |
| 121 | tests/integration/test_target_resolver.sh | PASS |
| 121 | tests/integration/test_security_policy.sh（正式 `security_policy` audit/enforce） | PASS |
| 122 | make agent + make security-policy-demo | PASS |
| 122 | tests/integration/test_target_resolver.sh | PASS |
| 122 | tests/integration/test_security_policy.sh（正式 `security_policy` audit/enforce） | PASS |
| 121 | --list-skills 输出正式 network_policy/network_qos/network_xdp/security_policy | PASS |
| 121 | --doctor-skills 返回 0 | PASS |
| 122 | make agent | PASS |
| 122 | --list-skills 输出正式 Network 子能力 | PASS |
| 122 | --doctor-skills 返回 0 | PASS |

## 6. TAP 质量门禁结果（17/17）

最新日志：`reports/final_quality_gate_20260621_security_cgroup_scope.log`

```
ok 1 - make agent
ok 2 - make network-policy-demo
ok 3 - make network-qos-tc
ok 4 - make network-xdp-demo
ok 5 - make security-policy-demo
ok 6 - --list-skills outputs formal network policy sub-skills
ok 7 - --doctor-skills exit 0
ok 8 - agent 15s smoke
ok 9 - network_policy default disabled
ok 10 - network_qos default disabled
ok 11 - network_xdp default disabled
ok 12 - security_policy default disabled
ok 13 - security_policy_demo default disabled
ok 14 - metrics default disabled on 127.0.0.1
ok 15 - dashboard index.html exists and non-empty
ok 16 - frozen result dirs exist
ok 17 - no BPF/LSM/TC/XDP residue
```

## 7. 最终判定

| 审计项 | 结果 |
|--------|------|
| 安全默认值 | PASS |
| BPF/cgroup 残留 | PASS |
| 内存泄漏 | WARN (Valgrind 工具限制，100 轮 smoke 替代) |
| 死锁/卡死 | PASS |
| 构建/回归 | PASS |
| 质量门禁 | PASS (17/17) |

**当前结论：EulerPilot 通过最新安全与质量审计，可作为当前争奖增强阶段的稳定基线。Security 已具备正式 `security_policy` 最小 audit/enforce 闭环、双 LSM enforce、四类 syscall tracing、最多 8 项 target_map、规则级 LSM blocked 事件标识和显式 cgroup scope；仍需继续补齐 Pod/container 自动解析、进程过滤和更多 LSM hook。**
