# Security Policy Integration 20260623-094234

运行环境：`192.168.1.121:/root/EulerPilot`，openEuler 24.03 LTS SP3。

运行命令：

```bash
cd /root/EulerPilot
bash tests/integration/test_security_policy.sh
```

结论：完整 Security Policy 集成测试通过。本轮在既有 file_open、bprm_check_security、socket_connect、file_access、PID/container/Pod scope 基础上，新增验证 `lsm/ptrace_traceme` scoped cgroup enforcement。

关键证据：

- `test.log`：包含 `PASS: security_policy ptrace_traceme blocks scoped ptrace in target cgroup`。
- `agent.ptrace.yaml` / `skills.ptrace.yaml`：本轮 ptrace scope-only cgroup target 配置。
- `security_policy_events.ptrace.jsonl`：blocked 事件包含 `event_hook=lsm_ptrace_traceme`、`path=ptrace_traceme`、`rule_id=deny_ptrace_traceme`、`target_ref=ptrace_scope` 和 cgroup 证据。
- `ptrace-outside.*`：scope 外 `PTRACE_TRACEME` 允许。
- `ptrace-blocked.*`：目标 cgroup 内 `PTRACE_TRACEME` 被拒绝。
- `ptrace-post-cleanup.*`：Agent 退出后恢复允许。

边界：`lsm_ptrace_traceme` 规则必须绑定解析后的 cgroup target，不作为全局 ptrace deny 使用。
