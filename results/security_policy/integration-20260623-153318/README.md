# Security Policy Integration Result 20260623-153318

来源：122 第二验证机 `/root/EulerPilot`。

本目录保存正式 `security_policy` 集成测试证据。该轮用于复核 121 上新增的只读目录保护能力：

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
