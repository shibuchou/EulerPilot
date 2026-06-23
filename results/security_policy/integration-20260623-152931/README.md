# Security Policy Integration Result 20260623-152931

来源：121 主验证机 `/root/EulerPilot`。

本目录保存正式 `security_policy` 集成测试证据。该轮在既有 file_open、bprm、socket_connect、ptrace_traceme、capable、PID/container/Pod scope 覆盖基础上，新增验证：

- `path_prefix + file_access=write + cgroup_path` 只读目录保护。
- 目标 cgroup 内目录前缀下文件写打开被 `lsm_file_open` 拒绝。
- 目标 cgroup 内读打开继续允许。
- scope 外写入继续允许。
- blocked 事件携带 `path_prefix`、`file_access=write`、`file_flags`、`cgroup_id`、`rule_id=deny_readonly_dir_write` 和 `target_ref=readonly_dir`。

关键入口：

- `test.log`
- `security_policy_events.readonly-dir.jsonl`
- `agent-readonly-dir.log`
- `skills.readonly-dir.yaml`
